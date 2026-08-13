// sim/src/intel_sync.cpp
//
// T058：层级化情报同步实现。
//
// 实现策略：
// - 直属可指挥单位实时同步：本节点所属单位即节点本地状态，每 tick 刷新
//   last_direct_sync_tick（FR-028 的"近乎实时"面向连排级本单位）；向上
//   传递仍按层级间隔，避免高层级同步风暴。
// - 层级同步沿指挥树自下而上合并：每个节点按其层级间隔拉取直接子节点的
//   情报记录并重标来源（direct → sync 标注具体发现单位；sync/relay →
//   relay 只标来源层级，FR-031）；位置/状态/敌情三类信息在同一同步事件
//   一次性携带（共用间隔，FR-028）。
// - 子节点失联（CP 单位 out_of_contact/被摧毁）时不合并新情报，同步事件
//   只携带 own/last_known 计数——冻结最后已知状态（FR-029/065）。
// - 遍历顺序固定（节点插入顺序 / 指挥树子节点顺序 / std::map 键序），
//   同一输入同一输出（宪法第 7 条）。

#include "wfs/sim/intel_sync.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/comm.h"
#include "wfs/sim/command_org.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel.h"

namespace wfs::sim {

namespace {

std::uint64_t ConfigTicks(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("intel_sync 配置必须为正: ") + key);
    }
    return value;
}

void LogSync(SimState& state, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kIntel, EventSeverity::kInfo, std::move(message));
}

std::string_view EchelonName(const model::Echelon echelon) {
    return model::to_string(echelon);
}

// 子节点是否失联：层级通信链路不生效（FR-077 取更严）或代表单位
// （列表内首个 node_id 命中的单位）失联/被摧毁。
bool ChildNodeLost(const SimState& state, const std::string& child_node_id, const std::string& parent_node_id) {
    if (!node_link_effective(state, child_node_id, parent_node_id)) {
        return true;
    }
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.node_id == child_node_id) {
            return unit.out_of_contact || unit.destroyed;
        }
    }
    return false;  // 无实体单位的节点视为未失联（无信息可冻结）。
}

struct ChildAggregate {
    std::size_t own_units = 0U;
    std::size_t out_of_contact = 0U;
};

ChildAggregate AggregateChildUnits(const SimState& state, const std::string& child_node_id) {
    ChildAggregate aggregate;
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.node_id != child_node_id) {
            continue;
        }
        ++aggregate.own_units;
        if (unit.out_of_contact) {
            ++aggregate.out_of_contact;
        }
    }
    return aggregate;
}

// 把子节点视角的情报重标来源后合并进父节点视角（FR-031 来源标注规则）。
// 返回合并的敌情条数；子节点失联时跳过合并（最后已知冻结）。
std::size_t MergeChildIntel(SimState& state, const std::string& child_node_id, const std::string& parent_node_id,
                            const model::Echelon child_echelon) {
    const std::uint64_t tick = state.clock.tick();
    std::size_t merged = 0U;
    for (const auto& [key, child_record] : state.intel_records) {
        if (child_record.observer_node_id != child_node_id) {
            continue;
        }
        IntelRecord relayed;
        relayed.observer_node_id = parent_node_id;
        relayed.target_unit_id = child_record.target_unit_id;
        relayed.tier = child_record.tier;
        relayed.last_seen_tick = tick;
        relayed.memory_until_tick = tick + state.intel_config.memory_ticks;
        relayed.source_expires_tick = tick + state.intel_config.source_expiry_ticks;
        relayed.last_known_x = child_record.last_known_x;
        relayed.last_known_y = child_record.last_known_y;
        relayed.last_motion_dx = child_record.last_motion_dx;
        relayed.last_motion_dy = child_record.last_motion_dy;
        if (child_record.source.kind == "direct") {
            // 子节点直属发现上报：标注具体发现单位与节点（FR-031）。
            relayed.source = IntelSource{"sync", child_record.source.unit_id, child_node_id, tick, ""};
        } else {
            // 子节点转发的更下层情报：只标注来源层级（"连级情报"等，FR-031）。
            relayed.source = IntelSource{"relay", "", "", tick, std::string(EchelonName(child_echelon))};
        }
        register_intel(state, relayed);
        LogSync(state, "INTEL_RELAY to=" + parent_node_id + " target=" + relayed.target_unit_id +
                           " tier=" + std::string(to_string(relayed.tier)) +
                           " source_kind=" + relayed.source.kind + " source_unit=" + relayed.source.unit_id +
                           " source_node=" + relayed.source.node_id + " source_level=" + relayed.source.level +
                           " tick=" + std::to_string(tick));
        ++merged;
    }
    return merged;
}

}  // namespace

std::uint64_t IntelSyncConfig::sync_ticks_for(const model::Echelon echelon) const noexcept {
    switch (echelon) {
        case model::Echelon::kPlatoon:
            return platoon_sync_ticks;
        case model::Echelon::kCompany:
            return company_sync_ticks;
        case model::Echelon::kBattalion:
            return battalion_sync_ticks;
        case model::Echelon::kBrigade:
            return brigade_sync_ticks;
        case model::Echelon::kSquad:
            return platoon_sync_ticks;  // 班不是指挥层级；保守取连排级间隔。
    }
    return platoon_sync_ticks;
}

IntelSyncConfig IntelSyncConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    IntelSyncConfig config;
    if (!raw.contains("intel_sync") || !raw["intel_sync"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["intel_sync"];
    config.platoon_sync_ticks = ConfigTicks(json, "platoon_sync_ticks", config.platoon_sync_ticks);
    config.company_sync_ticks = ConfigTicks(json, "company_sync_ticks", config.company_sync_ticks);
    config.battalion_sync_ticks = ConfigTicks(json, "battalion_sync_ticks", config.battalion_sync_ticks);
    config.brigade_sync_ticks = ConfigTicks(json, "brigade_sync_ticks", config.brigade_sync_ticks);
    if (json.contains("realtime_direct_units") && !json["realtime_direct_units"].is_boolean()) {
        throw std::invalid_argument("intel_sync.realtime_direct_units 必须是布尔值");
    }
    config.realtime_direct_units = json.value("realtime_direct_units", config.realtime_direct_units);
    if (!config.is_valid()) {
        throw std::invalid_argument("intel_sync 配置非法（间隔必须为正且随层级单调不降）");
    }
    return config;
}

void to_json(nlohmann::json& json, const IntelSyncState& state) {
    json = nlohmann::json{{"last_hierarchy_sync_tick", state.last_hierarchy_sync_tick},
                          {"last_direct_sync_tick", state.last_direct_sync_tick}};
}

void from_json(const nlohmann::json& json, IntelSyncState& state) {
    state.last_hierarchy_sync_tick =
        json.value("last_hierarchy_sync_tick", std::map<std::string, std::uint64_t>{});
    state.last_direct_sync_tick = json.value("last_direct_sync_tick", std::map<std::string, std::uint64_t>{});
}

void step_intel_sync(SimState& state) {
    if (!state.command_org.configured) {
        return;
    }
    const std::uint64_t tick = state.clock.tick();
    const IntelSyncConfig& config = state.intel_sync_config;
    const std::vector<std::string> nodes = state.command_org.nodes_in_insertion_order();

    // 1) 直属可指挥单位实时同步（每 tick 刷新，不产生高频事件）。
    if (config.realtime_direct_units) {
        for (const std::string& node_id : nodes) {
            const bool has_direct_units =
                std::any_of(state.units.begin(), state.units.end(),
                            [&](const RuntimeUnitState& unit) { return unit.node_id == node_id; });
            if (has_direct_units) {
                state.intel_sync_state.last_direct_sync_tick[node_id] = tick;
            }
        }
    }

    // 2) 层级同步（节点插入顺序固定；子节点顺序由指挥树保证）。
    for (const std::string& node_id : nodes) {
        const model::Echelon echelon = command_echelon(state.command_org, node_id);
        const std::uint64_t interval = config.sync_ticks_for(echelon);
        const std::uint64_t last_sync = state.intel_sync_state.last_hierarchy_sync_tick[node_id];
        if (tick < last_sync + interval) {
            continue;
        }
        state.intel_sync_state.last_hierarchy_sync_tick[node_id] = tick;
        for (const std::string& child_id : state.command_org.children_of(node_id)) {
            const bool child_lost = ChildNodeLost(state, child_id, node_id);
            std::size_t merged = 0U;
            if (!child_lost) {
                merged = MergeChildIntel(state, child_id, node_id, command_echelon(state.command_org, child_id));
            }
            const ChildAggregate aggregate = AggregateChildUnits(state, child_id);
            LogSync(state, "INTEL_SYNC node=" + node_id + " level=" + std::string(EchelonName(echelon)) +
                               " tick=" + std::to_string(tick) + " from=" + child_id +
                               " enemy=" + std::to_string(merged) + " own=" + std::to_string(aggregate.own_units) +
                               " last_known=" + std::to_string(aggregate.out_of_contact));
        }
    }
}

}  // namespace wfs::sim
