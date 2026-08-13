// sim/src/comm.cpp
//
// T060：通信状态模型实现。
//
// 实现策略：
// - 通信范围是纯确定性函数：base_range_km × 装备功率平均 × power_scale +
//   保障部队增益 × 保障系数 − 地形罚值（两端点较严者）+ 民用通讯设施增益，
//   下限保底 min_range_km；全部参数场景数据驱动（FR-077，宪法第 12 条）。
// - 链路 = 单位↔所属节点 + 子节点↔父节点；每 tick 按固定顺序判定连通，
//   中断时冻结最后已知位置并记录 COMM_OUTAGE，恢复独立记录 COMM_RESTORED。
// - 中断与失联取更严：link_effective = 通信连通 && 两端单位未失联/未摧毁；
//   失联恢复由 contact.cpp 独立处理，互不等待（CHK163/FR-077）。
// - 通信中断门控指令：到期命令的目标单位链路不生效时到达时间顺延到下一
//   tick（指令延迟到达），恢复后按原队列顺序继续；中断事件本身已可观察。
// - 遍历顺序固定（单位列表顺序/节点插入顺序），同一输入同一输出（宪法 7）。

#include "wfs/sim/comm.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/command_chain.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/movement.h"

namespace wfs::sim {

namespace {

double ConfigDouble(const nlohmann::json& json, const char* key, double fallback, double minimum) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < minimum) {
        throw std::invalid_argument(std::string("comm 配置非法: ") + key);
    }
    return value;
}

void LogComm(SimState& state, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kCommand, severity, std::move(message));
}

// 节点代表单位（列表内首个 node_id 命中者）；未找到返回 nullptr。
const RuntimeUnitState* NodeUnit(const SimState& state, const std::string& node_id) {
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.node_id == node_id) {
            return &unit;
        }
    }
    return nullptr;
}

const RuntimeUnitState* FindUnitById(const SimState& state, const std::string& unit_id) {
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.id == unit_id) {
            return &unit;
        }
    }
    return nullptr;
}

// (x,y) 所在首个地形网格单元的 terrain_id；无匹配返回空串。
std::string TerrainIdAt(const SimState& state, const double x, const double y) {
    for (const TerrainCell& cell : state.terrain_cells) {
        if (x >= cell.x && x <= cell.x + cell.width_km && y >= cell.y && y <= cell.y + cell.height_km) {
            return cell.terrain_id;
        }
    }
    return {};
}

double TerrainPenalty(const CommConfig& config, const std::string& terrain_id) {
    const auto iterator = config.terrain_penalty_km.find(terrain_id);
    return iterator == config.terrain_penalty_km.end() ? 0.0 : iterator->second;
}

// 任一端点位于民用通讯设施半径内即获得增益（FR-024/077）。
double CivilianFacilityBonus(const CommConfig& config, const double from_x, const double from_y, const double to_x,
                             const double to_y) {
    for (const CivilianCommFacility& facility : config.civilian_facilities) {
        const double dx_from = from_x - facility.x;
        const double dy_from = from_y - facility.y;
        const double dx_to = to_x - facility.x;
        const double dy_to = to_y - facility.y;
        if (std::sqrt((dx_from * dx_from) + (dy_from * dy_from)) <= facility.radius_km ||
            std::sqrt((dx_to * dx_to) + (dy_to * dy_to)) <= facility.radius_km) {
            return config.civilian_facility_bonus_km;
        }
    }
    return 0.0;
}

// 节点保障系数：节点拥有通信保障部队时，任一存活即满增益；全部失能则按
// support_disabled_factor 缩小范围（FR-077）；无保障部队不依赖、系数为 1。
double SupportFactorForNode(const SimState& state, const CommConfig& config, const std::string& node_id) {
    bool has_support = false;
    bool operational_support = false;
    for (const auto& [unit_id, profile] : state.comm_units) {
        if (!profile.is_support()) {
            continue;
        }
        const RuntimeUnitState* unit = FindUnitById(state, unit_id);
        if (unit == nullptr || unit->node_id != node_id) {
            continue;
        }
        has_support = true;
        if (!unit->destroyed && !unit->out_of_contact) {
            operational_support = true;
        }
    }
    if (!has_support) {
        return 1.0;
    }
    return operational_support ? 1.0 : config.support_disabled_factor;
}

// 链路端点代表位置（单位自身位置 / 节点代表单位位置）。
bool LinkEndpoints(const SimState& state, const CommLinkStatus& link, double& from_x, double& from_y, double& to_x,
                   double& to_y, const RuntimeUnitState*& from_unit, const RuntimeUnitState*& to_unit) {
    if (link.kind == CommLinkKind::kUnit) {
        from_unit = FindUnitById(state, link.from_id);
        to_unit = NodeUnit(state, link.to_id);
    } else {
        from_unit = NodeUnit(state, link.from_id);
        to_unit = NodeUnit(state, link.to_id);
    }
    if (from_unit == nullptr || to_unit == nullptr) {
        return false;
    }
    from_x = from_unit->x;
    from_y = from_unit->y;
    to_x = to_unit->x;
    to_y = to_unit->y;
    return true;
}

}  // namespace

std::string_view to_string(const CommLinkKind kind) noexcept {
    switch (kind) {
        case CommLinkKind::kUnit:
            return "unit";
        case CommLinkKind::kNode:
            return "node";
    }
    return "unknown";
}

CommLinkKind comm_link_kind_from_string(const std::string_view name) {
    if (name == "unit") {
        return CommLinkKind::kUnit;
    }
    if (name == "node") {
        return CommLinkKind::kNode;
    }
    throw std::invalid_argument("未知通信链路类型: " + std::string(name));
}

void to_json(nlohmann::json& json, const CommLinkKind kind) {
    json = to_string(kind);
}

void from_json(const nlohmann::json& json, CommLinkKind& kind) {
    kind = comm_link_kind_from_string(json.get<std::string>());
}

CommConfig CommConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    CommConfig config;
    if (!raw.contains("comm") || !raw["comm"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["comm"];
    config.base_range_km = ConfigDouble(json, "base_range_km", config.base_range_km, 0.0);
    config.power_scale = ConfigDouble(json, "power_scale", config.power_scale, 0.0);
    config.support_force_bonus_km = ConfigDouble(json, "support_force_bonus_km", config.support_force_bonus_km, 0.0);
    config.support_disabled_factor = ConfigDouble(json, "support_disabled_factor", config.support_disabled_factor, 0.0);
    config.civilian_facility_bonus_km =
        ConfigDouble(json, "civilian_facility_bonus_km", config.civilian_facility_bonus_km, 0.0);
    config.min_range_km = ConfigDouble(json, "min_range_km", config.min_range_km, 0.0);
    if (config.support_disabled_factor > 1.0) {
        throw std::invalid_argument("comm.support_disabled_factor 必须在 [0,1] 内");
    }
    if (json.contains("terrain_penalty_km")) {
        for (const auto& [terrain_id, penalty] : json["terrain_penalty_km"].items()) {
            if (!penalty.is_number() || !std::isfinite(penalty.get<double>()) || penalty.get<double>() < 0.0) {
                throw std::invalid_argument("comm.terrain_penalty_km 罚值必须为非负有限数: " + terrain_id);
            }
            config.terrain_penalty_km[terrain_id] = penalty.get<double>();
        }
    }
    if (json.contains("civilian_facilities")) {
        for (const nlohmann::json& facility : json["civilian_facilities"]) {
            CivilianCommFacility entry;
            entry.id = facility.at("id").get<std::string>();
            entry.x = facility.at("x").get<double>();
            entry.y = facility.at("y").get<double>();
            entry.radius_km = facility.at("radius_km").get<double>();
            if (!std::isfinite(entry.x) || !std::isfinite(entry.y) || !std::isfinite(entry.radius_km) ||
                entry.radius_km < 0.0) {
                throw std::invalid_argument("comm.civilian_facilities 数值非法: " + entry.id);
            }
            config.civilian_facilities.push_back(std::move(entry));
        }
    }
    if (!config.is_valid()) {
        throw std::invalid_argument("comm 配置非法");
    }
    return config;
}

bool CommConfig::is_valid() const noexcept {
    if (!std::isfinite(base_range_km) || base_range_km < 0.0 || !std::isfinite(power_scale) || power_scale < 0.0 ||
        !std::isfinite(support_force_bonus_km) || support_force_bonus_km < 0.0 ||
        !std::isfinite(support_disabled_factor) || support_disabled_factor < 0.0 || support_disabled_factor > 1.0 ||
        !std::isfinite(civilian_facility_bonus_km) || civilian_facility_bonus_km < 0.0 ||
        !std::isfinite(min_range_km) || min_range_km < 0.0) {
        return false;
    }
    for (const auto& [terrain_id, penalty] : terrain_penalty_km) {
        if (terrain_id.empty() || !std::isfinite(penalty) || penalty < 0.0) {
            return false;
        }
    }
    for (const CivilianCommFacility& facility : civilian_facilities) {
        if (facility.id.empty() || !std::isfinite(facility.x) || !std::isfinite(facility.y) ||
            !std::isfinite(facility.radius_km) || facility.radius_km < 0.0) {
            return false;
        }
    }
    return true;
}

double effective_comm_range_km(const CommConfig& config, const double power_from, const double power_to,
                               const double support_factor, const std::string& terrain_from,
                               const std::string& terrain_to, const double from_x, const double from_y,
                               const double to_x, const double to_y) {
    const double power_factor = (power_from + power_to) * 0.5;
    const double terrain_penalty = std::max(TerrainPenalty(config, terrain_from), TerrainPenalty(config, terrain_to));
    const double civilian_bonus = CivilianFacilityBonus(config, from_x, from_y, to_x, to_y);
    const double range = (config.base_range_km * power_factor * config.power_scale) +
                         (config.support_force_bonus_km * support_factor) - terrain_penalty + civilian_bonus;
    return std::max(range, config.min_range_km);
}

const CommLinkStatus* CommState::Find(const CommLinkKind kind, const std::string& from_id,
                                      const std::string& to_id) const {
    for (const CommLinkStatus& link : links) {
        if (link.kind == kind && link.from_id == from_id && link.to_id == to_id) {
            return &link;
        }
    }
    return nullptr;
}

std::size_t CommState::outage_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(links.begin(), links.end(), [](const CommLinkStatus& link) { return !link.connected; }));
}

void to_json(nlohmann::json& json, const CommLinkStatus& link) {
    json = nlohmann::json{{"id", link.id},
                          {"kind", link.kind},
                          {"from_id", link.from_id},
                          {"to_id", link.to_id},
                          {"connected", link.connected},
                          {"reason", link.reason},
                          {"outage_start_tick", link.outage_start_tick},
                          {"restored_tick", link.restored_tick},
                          {"last_known_x", link.last_known_x},
                          {"last_known_y", link.last_known_y},
                          {"last_known_tick", link.last_known_tick}};
}

void from_json(const nlohmann::json& json, CommLinkStatus& link) {
    link.id = json.at("id").get<std::string>();
    link.kind = json.at("kind").get<CommLinkKind>();
    link.from_id = json.at("from_id").get<std::string>();
    link.to_id = json.at("to_id").get<std::string>();
    link.connected = json.at("connected").get<bool>();
    link.reason = json.value("reason", std::string());
    link.outage_start_tick = json.value("outage_start_tick", 0U);
    link.restored_tick = json.value("restored_tick", 0U);
    link.last_known_x = json.value("last_known_x", 0.0);
    link.last_known_y = json.value("last_known_y", 0.0);
    link.last_known_tick = json.value("last_known_tick", 0U);
}

void to_json(nlohmann::json& json, const CommState& state) {
    json = nlohmann::json{{"links", state.links}};
}

void from_json(const nlohmann::json& json, CommState& state) {
    state.links = json.at("links").get<std::vector<CommLinkStatus>>();
}

bool link_effective(const SimState& state, const CommLinkStatus& link) {
    if (!link.connected) {
        return false;
    }
    double from_x = 0.0;
    double from_y = 0.0;
    double to_x = 0.0;
    double to_y = 0.0;
    const RuntimeUnitState* from_unit = nullptr;
    const RuntimeUnitState* to_unit = nullptr;
    if (!LinkEndpoints(state, link, from_x, from_y, to_x, to_y, from_unit, to_unit)) {
        return false;
    }
    (void)from_x;
    (void)from_y;
    (void)to_x;
    (void)to_y;
    const bool endpoint_disabled =
        from_unit->destroyed || from_unit->out_of_contact || to_unit->destroyed || to_unit->out_of_contact;
    return !endpoint_disabled;
}

bool unit_link_effective(const SimState& state, const std::string& unit_id) {
    const RuntimeUnitState* unit = FindUnitById(state, unit_id);
    if (unit == nullptr) {
        return true;
    }
    const CommLinkStatus* link = state.comm_state.Find(CommLinkKind::kUnit, unit_id, unit->node_id);
    return link == nullptr || link_effective(state, *link);
}

bool node_link_effective(const SimState& state, const std::string& child_node_id, const std::string& parent_node_id) {
    const CommLinkStatus* link = state.comm_state.Find(CommLinkKind::kNode, child_node_id, parent_node_id);
    return link == nullptr || link_effective(state, *link);
}

void initialize_comm_state(SimState& state) {
    state.comm_state.links.clear();
    if (!state.command_org.configured) {
        return;
    }
    std::uint64_t next_id = 0U;
    for (const RuntimeUnitState& unit : state.units) {
        state.comm_state.links.push_back(CommLinkStatus{"link-" + std::to_string(next_id++), CommLinkKind::kUnit,
                                                        unit.id, unit.node_id, true, "", 0U, 0U, 0.0, 0.0, 0U});
    }
    for (const std::string& node_id : state.command_org.nodes_in_insertion_order()) {
        const model::CommandNode* node = state.command_org.find_node(node_id);
        if (node == nullptr || node->parent_id.empty()) {
            continue;
        }
        state.comm_state.links.push_back(CommLinkStatus{"link-" + std::to_string(next_id++), CommLinkKind::kNode,
                                                        node_id, node->parent_id, true, "", 0U, 0U, 0.0, 0.0, 0U});
    }
}

void step_comm(SimState& state) {
    if (!state.command_org.configured) {
        return;
    }
    const std::uint64_t tick = state.clock.tick();
    const CommConfig& config = state.comm_config;
    for (CommLinkStatus& link : state.comm_state.links) {
        double from_x = 0.0;
        double from_y = 0.0;
        double to_x = 0.0;
        double to_y = 0.0;
        const RuntimeUnitState* from_unit = nullptr;
        const RuntimeUnitState* to_unit = nullptr;
        if (!LinkEndpoints(state, link, from_x, from_y, to_x, to_y, from_unit, to_unit)) {
            continue;
        }
        // 端点档案按代表单位 id 查表；缺失按基线（功率 1.0、standard）。
        const auto from_profile_it = state.comm_units.find(from_unit->id);
        const auto to_profile_it = state.comm_units.find(to_unit->id);
        const CommUnitProfile from_profile =
            from_profile_it == state.comm_units.end() ? CommUnitProfile{} : from_profile_it->second;
        const CommUnitProfile to_profile =
            to_profile_it == state.comm_units.end() ? CommUnitProfile{} : to_profile_it->second;
        const std::string support_node = link.kind == CommLinkKind::kUnit ? link.to_id : link.from_id;
        const double support_factor = SupportFactorForNode(state, config, support_node);
        const double range = effective_comm_range_km(config, from_profile.power, to_profile.power, support_factor,
                                                     TerrainIdAt(state, from_x, from_y), TerrainIdAt(state, to_x, to_y),
                                                     from_x, from_y, to_x, to_y);
        const double distance = std::sqrt(((from_x - to_x) * (from_x - to_x)) + ((from_y - to_y) * (from_y - to_y)));
        const bool endpoints_destroyed = from_unit->destroyed || to_unit->destroyed;
        const bool out_of_range = distance > range;
        const bool support_lost = support_factor < 1.0 && out_of_range;
        const bool now_connected = !endpoints_destroyed && !out_of_range;
        const std::string reason = endpoints_destroyed ? "ENDPOINT_DISABLED"
                                   : support_lost      ? "SUPPORT_DISABLED"
                                   : out_of_range      ? "OUT_OF_RANGE"
                                                       : "";

        if (now_connected) {
            link.last_known_x = from_x;
            link.last_known_y = from_y;
            link.last_known_tick = tick;
        }
        if (link.connected && !now_connected) {
            link.connected = false;
            link.reason = reason;
            link.outage_start_tick = tick;
            LogComm(state, EventSeverity::kWarning,
                    "COMM_OUTAGE kind=" + std::string(to_string(link.kind)) + " from=" + link.from_id + " to=" +
                        link.to_id + " reason=" + link.reason + " tick=" + std::to_string(tick) + " last_known=(" +
                        std::to_string(link.last_known_x) + "," + std::to_string(link.last_known_y) + ")");
        } else if (!link.connected && now_connected) {
            link.connected = true;
            link.reason.clear();
            link.restored_tick = tick;
            LogComm(state, EventSeverity::kInfo,
                    "COMM_RESTORED kind=" + std::string(to_string(link.kind)) + " from=" + link.from_id +
                        " to=" + link.to_id + " tick=" + std::to_string(tick));
        }
    }

    // 通信中断门控指令（FR-077）：到期命令的目标单位链路不生效时到达时间
    // 顺延，恢复后按原队列顺序继续；COMM_OUTAGE/COMM_RESTORED 已可观察。
    for (const ChainCommand& command : state.command_chain.CommandsInIssueOrder()) {
        if (command.unit_id.empty() || command.arrival_tick > tick ||
            (command.state != CommandState::kIssued && command.state != CommandState::kAcknowledged)) {
            continue;
        }
        if (unit_link_effective(state, command.unit_id)) {
            continue;
        }
        if (ChainCommand* mutable_command = state.command_chain.FindMutable(command.command_id)) {
            mutable_command->arrival_tick = tick + 1U;
        }
    }
}

}  // namespace wfs::sim
