// sim/src/intel.cpp
//
// T033：迷雾/情报识别系统实现。
//
// 实现策略：
// - 观察评分是纯确定性公式：能力 × 距离衰减 × 隐蔽衰减 × 移动/尺寸增益 ×
//   环境 × 烟幕；评分 < T1 阈值不可见，档位按阈值分档（T1–T3）。
// - 记忆保留：完全脱离视野后，记录在 memory_until_tick 前保留原档位
//   （已识别不降档），超时删除；来源标注按 source_expiry_ticks 独立过期
//   （过期只清来源，不删记忆）。
// - 最后动向：连续两次观察到位置变化时记录归一化方向（FR-035）。
// - 遍历顺序固定（单位列表顺序 / std::map 键序），std::map 序列化键序确定，
//   保证同一输入同一输出（宪法第 7 条）。

#include "wfs/sim/intel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/contact.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/movement.h"

namespace wfs::sim {

namespace {

constexpr double kEpsilon = 1e-9;              // 位置变化判定容差（km）。
constexpr double kMinAbility = 0.5;            // 无经验数据时的基线观察能力。
constexpr double kAbilityFloor = 0.5;          // 有效可视距离的能力下限系数。
constexpr double kObserverAbilityBase = 0.5;   // 观察能力基线。
constexpr double kObserverAbilityScale = 0.5;  // 战斗经验对观察能力的贡献。

double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("intel 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("intel 配置必须为正: ") + key);
    }
    return value;
}

void LogIntel(SimState& state, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kIntel, severity, std::move(message));
}

// 目标装备构成文本（T033 登记项 F5）：按装备声明顺序取名称（缺省回退 id），
// 去重后逗号拼接；无装备返回空串。
std::string CompositionOf(const RuntimeUnitState& target) {
    std::string composition;
    std::vector<std::string> seen;
    for (const model::Weapon& weapon : target.weapons) {
        const std::string label = weapon.name.empty() ? weapon.id : weapon.name;
        if (std::find(seen.begin(), seen.end(), label) != seen.end()) {
            continue;
        }
        seen.push_back(label);
        if (!composition.empty()) {
            composition += ",";
        }
        composition += label;
    }
    return composition;
}

double ObserverAbility(const RuntimeUnitState& observer) {
    if (observer.soldiers.empty()) {
        return kMinAbility;
    }
    // 基线 + 经验三维中的战斗经验贡献（v1 数值基线，数据可扩展）。
    return Clamp01(kObserverAbilityBase + (kObserverAbilityScale * observer.soldiers.front().experience.combat));
}

}  // namespace

IntelConfig IntelConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    IntelConfig config;
    if (!raw.contains("intel") || !raw["intel"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["intel"];
    config.base_visibility_range_km = ConfigDouble(json, "base_visibility_range_km", config.base_visibility_range_km);
    config.observer_ability_scale = ConfigDouble(json, "observer_ability_scale", config.observer_ability_scale);
    config.t1_threshold = ConfigDouble(json, "t1_threshold", config.t1_threshold);
    config.t2_threshold = ConfigDouble(json, "t2_threshold", config.t2_threshold);
    config.t3_threshold = ConfigDouble(json, "t3_threshold", config.t3_threshold);
    config.concealment_penalty = ConfigDouble(json, "concealment_penalty", config.concealment_penalty);
    config.moving_concealment_reduction =
        ConfigDouble(json, "moving_concealment_reduction", config.moving_concealment_reduction);
    config.vehicle_size_bonus = ConfigDouble(json, "vehicle_size_bonus", config.vehicle_size_bonus);
    config.smoke_obscuration = ConfigDouble(json, "smoke_obscuration", config.smoke_obscuration);
    config.degraded_observation_factor =
        ConfigDouble(json, "degraded_observation_factor", config.degraded_observation_factor);
    config.memory_ticks = ConfigUint64(json, "memory_ticks", config.memory_ticks);
    config.source_expiry_ticks = ConfigUint64(json, "source_expiry_ticks", config.source_expiry_ticks);
    if (!config.is_valid()) {
        throw std::invalid_argument("intel 配置非法（阈值须 0 < t1 <= t2 <= t3 <= 1）");
    }
    return config;
}

std::string_view to_string(const RecognitionTier tier) noexcept {
    switch (tier) {
        case RecognitionTier::kNone:
            return "none";
        case RecognitionTier::kT1:
            return "T1";
        case RecognitionTier::kT2:
            return "T2";
        case RecognitionTier::kT3:
            return "T3";
    }
    return "unknown";
}

RecognitionTier recognition_tier_from_string(const std::string_view name) {
    if (name == "none") {
        return RecognitionTier::kNone;
    }
    if (name == "T1") {
        return RecognitionTier::kT1;
    }
    if (name == "T2") {
        return RecognitionTier::kT2;
    }
    if (name == "T3") {
        return RecognitionTier::kT3;
    }
    throw std::invalid_argument("未知识别档位: " + std::string(name));
}

void to_json(nlohmann::json& json, const RecognitionTier tier) {
    json = to_string(tier);
}

void from_json(const nlohmann::json& json, RecognitionTier& tier) {
    tier = recognition_tier_from_string(json.get<std::string>());
}

ObservationResult resolve_observation(const ObservationInput& input, const IntelConfig& config) {
    ObservationResult result;
    const double delta_x = input.target_x - input.observer_x;
    const double delta_y = input.target_y - input.observer_y;
    result.distance_km = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
    result.effective_range_km = config.base_visibility_range_km *
                                (kAbilityFloor + (Clamp01(input.observer_ability) * config.observer_ability_scale));
    const double range_factor =
        result.effective_range_km > 0.0 ? Clamp01(1.0 - (result.distance_km / result.effective_range_km)) : 0.0;
    const double concealment_factor = 1.0 - (Clamp01(input.target_concealment) * config.concealment_penalty);
    const double moving_factor = input.target_moving ? 1.0 + config.moving_concealment_reduction : 1.0;
    const double vehicle_factor = input.target_is_vehicle ? 1.0 + config.vehicle_size_bonus : 1.0;
    const double smoke_factor = input.target_in_smoke ? 1.0 - config.smoke_obscuration : 1.0;
    result.score = Clamp01(input.observer_ability) * range_factor * concealment_factor * moving_factor *
                   vehicle_factor * smoke_factor * Clamp01(input.environment_visibility_multiplier);
    result.visible = result.distance_km <= result.effective_range_km && result.score >= config.t1_threshold;
    return result;
}

RecognitionTier recognition_tier(const double score, const IntelConfig& config) {
    if (score < config.t1_threshold) {
        return RecognitionTier::kNone;
    }
    if (score < config.t2_threshold) {
        return RecognitionTier::kT1;
    }
    if (score < config.t3_threshold) {
        return RecognitionTier::kT2;
    }
    return RecognitionTier::kT3;
}

void to_json(nlohmann::json& json, const IntelSource& source) {
    json = nlohmann::json{{"kind", source.kind},
                          {"unit_id", source.unit_id},
                          {"node_id", source.node_id},
                          {"reported_tick", source.reported_tick}};
    // 旧存档无 level：空值省略，加载后再次序列化保持字节一致（宪法 13）。
    if (!source.level.empty()) {
        json["level"] = source.level;
    }
}

void from_json(const nlohmann::json& json, IntelSource& source) {
    source.kind = json.at("kind").get<std::string>();
    source.unit_id = json.at("unit_id").get<std::string>();
    source.node_id = json.at("node_id").get<std::string>();
    source.reported_tick = json.at("reported_tick").get<std::uint64_t>();
    source.level = json.value("level", std::string());
}

void to_json(nlohmann::json& json, const IntelRecord& record) {
    json = nlohmann::json{{"observer_node_id", record.observer_node_id},
                          {"target_unit_id", record.target_unit_id},
                          {"tier", record.tier},
                          {"last_seen_tick", record.last_seen_tick},
                          {"memory_until_tick", record.memory_until_tick},
                          {"source_expires_tick", record.source_expires_tick},
                          {"source", record.source},
                          {"last_known_x", record.last_known_x},
                          {"last_known_y", record.last_known_y},
                          {"last_motion_dx", record.last_motion_dx},
                          {"last_motion_dy", record.last_motion_dy},
                          {"observed_count", record.observed_count},
                          {"type_name", record.type_name},
                          {"composition", record.composition}};
}

void from_json(const nlohmann::json& json, IntelRecord& record) {
    record.observer_node_id = json.at("observer_node_id").get<std::string>();
    record.target_unit_id = json.at("target_unit_id").get<std::string>();
    record.tier = json.at("tier").get<RecognitionTier>();
    record.last_seen_tick = json.at("last_seen_tick").get<std::uint64_t>();
    record.memory_until_tick = json.at("memory_until_tick").get<std::uint64_t>();
    record.source_expires_tick = json.at("source_expires_tick").get<std::uint64_t>();
    record.source = json.at("source").get<IntelSource>();
    record.last_known_x = json.at("last_known_x").get<double>();
    record.last_known_y = json.at("last_known_y").get<double>();
    record.last_motion_dx = json.at("last_motion_dx").get<double>();
    record.last_motion_dy = json.at("last_motion_dy").get<double>();
    record.observed_count = json.value("observed_count", 0U);
    record.type_name = json.value("type_name", std::string());
    record.composition = json.value("composition", std::string());
}

std::string intel_record_key(const std::string_view observer_node_id, const std::string_view target_unit_id) {
    return std::string(observer_node_id) + ":" + std::string(target_unit_id);
}

RecognitionTier effective_tier(const IntelRecord& record, const std::uint64_t tick, const IntelConfig& /*config*/) {
    if (tick >= record.memory_until_tick) {
        return RecognitionTier::kNone;
    }
    return record.tier;
}

const IntelRecord* find_intel(const SimState& state, const std::string_view observer_node_id,
                              const std::string_view target_unit_id) {
    const auto iterator = state.intel_records.find(intel_record_key(observer_node_id, target_unit_id));
    return iterator == state.intel_records.end() ? nullptr : &iterator->second;
}

IntelRecord* find_intel_mutable(SimState& state, const std::string_view observer_node_id,
                                const std::string_view target_unit_id) {
    auto iterator = state.intel_records.find(intel_record_key(observer_node_id, target_unit_id));
    return iterator == state.intel_records.end() ? nullptr : &iterator->second;
}

void register_intel(SimState& state, const IntelRecord& record) {
    IntelRecord& target = state.intel_records[intel_record_key(record.observer_node_id, record.target_unit_id)];
    // 键内两字段必须同步写回：新建记录经 operator[] 默认构造时为空，缺失
    // 会导致后续按 observer_node_id 过滤（如 T058 层级合并）静默漏项。
    target.observer_node_id = record.observer_node_id;
    target.target_unit_id = record.target_unit_id;
    target.tier = std::max(target.tier, record.tier);
    if (record.last_seen_tick >= target.last_seen_tick) {
        target.last_seen_tick = record.last_seen_tick;
        target.memory_until_tick = record.memory_until_tick;
        target.source_expires_tick = record.source_expires_tick;
        target.source = record.source;
        target.last_known_x = record.last_known_x;
        target.last_known_y = record.last_known_y;
        target.last_motion_dx = record.last_motion_dx;
        target.last_motion_dy = record.last_motion_dy;
    }
}

namespace {

// 使用预建地形索引的观察结算（M6：step_intel 每 tick 只构建一次索引）。
bool ObservePairWithIndex(SimState& state, const RuntimeUnitState& observer, const RuntimeUnitState& target,
                          const std::map<std::string, const model::TerrainElement*>& terrain_index) {
    const TerrainSample terrain = terrain_sample_at(terrain_index, state.terrain_cells, target.x, target.y);
    const double smoke = smoke_concealment_at(state.smoke_areas, target.x, target.y);
    const double environment_visibility =
        state.scenario.raw.value("environment", nlohmann::json::object()).value("visibility_multiplier", 1.0);
    double observer_ability = ObserverAbility(observer);
    // M5：观瞄模块失效阻断观察；降级/压制按有效观察能力衰减（data-model §6）。
    const EffectSeverity observation_effect = effective_observation_effect(observer, state.contact_config);
    if (observation_effect == EffectSeverity::kDisabled) {
        return false;
    }
    if (observation_effect == EffectSeverity::kDegraded) {
        observer_ability *= state.intel_config.degraded_observation_factor;
    }
    const ObservationResult observation = resolve_observation(
        ObservationInput{observer.x, observer.y, observer_ability, target.x, target.y, terrain.concealment,
                         target.is_vehicle, target.moving, environment_visibility, smoke > 0.0},
        state.intel_config);
    if (!observation.visible) {
        return false;
    }

    const std::string key = intel_record_key(observer.node_id, target.id);
    const bool existed = state.intel_records.contains(key);
    IntelRecord& record = state.intel_records[key];
    const RecognitionTier computed = recognition_tier(observation.score, state.intel_config);
    const RecognitionTier previous = record.tier;
    record.observer_node_id = observer.node_id;
    record.target_unit_id = target.id;
    record.tier = std::max(previous, computed);  // 已识别不降档（记忆保留）。
    // T033 登记项（F5）：识别档位核心字段随观察输出（数量/类型/构成）。
    record.observed_count = target.soldiers.size();
    record.type_name = target.type;
    record.composition = CompositionOf(target);
    if (!existed) {
        LogIntel(state, EventSeverity::kInfo,
                 "INTEL_OBSERVED observer=" + observer.id + " target=" + target.id +
                     " tier=" + std::string(to_string(record.tier)) + " source=direct source_unit=" + observer.id +
                     " source_node=" + observer.node_id + " observed_count=" + std::to_string(record.observed_count) +
                     " type_name=" + record.type_name + " composition=" + record.composition);
    }
    if (computed > previous) {
        LogIntel(state, EventSeverity::kInfo,
                 "INTEL_RECOGNIZED observer=" + observer.id + " target=" + target.id +
                     " tier=" + std::string(to_string(computed)));
    }
    // 最后动向（FR-035）：相对上一次目视位置的归一化方向。首次目视可能
    // 发生在 tick 0，因此以 memory_until_tick 是否已设置判定"已有上一次
    // 目视位置"（tick 0 首次目视时 memory_until_tick 也会被设置）。
    if (record.memory_until_tick > 0U) {
        const double delta_x = target.x - record.last_known_x;
        const double delta_y = target.y - record.last_known_y;
        const double length = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
        if (length > kEpsilon) {
            record.last_motion_dx = delta_x / length;
            record.last_motion_dy = delta_y / length;
        }
    }
    record.last_known_x = target.x;
    record.last_known_y = target.y;
    record.last_seen_tick = state.clock.tick();
    record.memory_until_tick = state.clock.tick() + state.intel_config.memory_ticks;
    record.source = IntelSource{"direct", observer.id, observer.node_id, state.clock.tick()};
    record.source_expires_tick = state.clock.tick() + state.intel_config.source_expiry_ticks;
    return true;
}

}  // namespace

bool observe_pair(SimState& state, const RuntimeUnitState& observer, const RuntimeUnitState& target) {
    std::map<std::string, const model::TerrainElement*> terrain_index;
    for (const model::TerrainElement& element : state.terrain_library) {
        terrain_index.emplace(element.id, &element);
    }
    return ObservePairWithIndex(state, observer, target, terrain_index);
}

void step_intel(SimState& state) {
    // M6：地形索引每 tick 构建一次，供全部观察对复用（消除逐单位 map 重建）。
    std::map<std::string, const model::TerrainElement*> terrain_index;
    for (const model::TerrainElement& element : state.terrain_library) {
        terrain_index.emplace(element.id, &element);
    }
    // 距离粗筛上界：最高观察能力下的有效可视距离（平方比较，避免 sqrt）。
    const double max_range =
        state.intel_config.base_visibility_range_km * (kAbilityFloor + state.intel_config.observer_ability_scale);
    const double max_range_sq = max_range * max_range;
    // 全量观察：固定单位顺序（观察方在外层，目标在内层）。
    for (const RuntimeUnitState& observer : state.units) {
        if (observer.destroyed || observer.out_of_contact) {
            continue;  // 失联/摧毁单位不能上报新情报。
        }
        for (const RuntimeUnitState& target : state.units) {
            if (target.side == observer.side || target.destroyed) {
                continue;
            }
            const double delta_x = target.x - observer.x;
            const double delta_y = target.y - observer.y;
            if ((delta_x * delta_x) + (delta_y * delta_y) > max_range_sq) {
                continue;  // 距离粗筛：超出最大可视距离不采样地形。
            }
            ObservePairWithIndex(state, observer, target, terrain_index);
        }
    }

    // 记忆/来源过期清理（std::map 键序确定；来源过期不删记忆）。
    for (auto iterator = state.intel_records.begin(); iterator != state.intel_records.end();) {
        IntelRecord& record = iterator->second;
        if (state.clock.tick() >= record.memory_until_tick) {
            LogIntel(state, EventSeverity::kInfo, "INTEL_MEMORY_LOST target=" + record.target_unit_id);
            iterator = state.intel_records.erase(iterator);
            continue;
        }
        if (!record.source.kind.empty() && record.source.kind != "expired" &&
            state.clock.tick() >= record.source_expires_tick) {
            LogIntel(state, EventSeverity::kInfo, "INTEL_SOURCE_EXPIRED target=" + record.target_unit_id);
            record.source = IntelSource{"expired", "", "", state.clock.tick()};
        }
        ++iterator;
    }
}

}  // namespace wfs::sim
