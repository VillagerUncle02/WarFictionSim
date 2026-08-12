// sim/src/contact.cpp
//
// T032：失联机制实现。
//
// 实现策略：
// - 失联概率/时长全部经统一 RNG 判定（宪法第 7 条）：概率 = 损伤概率 ×
//   损伤比例 + 压制概率 × 压制值（压制达阈值才计入），未命中直接返回；
//   恢复时长在 [min_ticks, max_ticks] 内均匀抽样（60–180s 基线可配置）。
// - 最后已知状态在触发失联时由调用方（combat.cpp）捕获并写入单位；
//   失联期间 effective_unit_state 返回冻结视图，恢复后回到实际状态。
// - 复合状态按"取最严"叠加：none < degraded < disabled；压制达阈值 →
//   degraded，失联/观瞄或移动模块 disabled → disabled，摧毁 → disabled。
// - step_contact 只做恢复倒计时（战斗触发、恢复独立结算，FR-065）。

#include "wfs/sim/contact.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/event_log.h"

namespace wfs::sim {

namespace {

constexpr std::uint32_t kProbabilityScale = 1000U;  // 概率判定统一比例尺。
constexpr std::uint32_t kMaxBoundedSpan = 1000000U;
double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("contact 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("contact 配置必须为正: ") + key);
    }
    return value;
}

EffectSeverity ModuleSeverity(model::ModuleState state) noexcept {
    switch (state) {
        case model::ModuleState::kFunctional:
            return EffectSeverity::kNone;
        case model::ModuleState::kDegraded:
            return EffectSeverity::kDegraded;
        case model::ModuleState::kDisabled:
            return EffectSeverity::kDisabled;
    }
    return EffectSeverity::kNone;
}

}  // namespace

ContactConfig ContactConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    ContactConfig config;
    const nlohmann::json* section = nullptr;
    if (raw.contains("contact") && raw["contact"].is_object()) {
        section = &raw["contact"];
    } else if (raw.contains("combat") && raw["combat"].is_object()) {
        section = &raw["combat"];  // 兼容旧 combat.contact_loss_* 键。
    }
    if (section == nullptr) {
        return config;
    }
    if (raw.contains("contact") && raw["contact"].is_object()) {
        config.suppression_threshold = ConfigDouble(*section, "suppression_threshold", config.suppression_threshold);
        config.damage_probability = ConfigDouble(*section, "damage_probability", config.damage_probability);
        config.suppression_probability =
            ConfigDouble(*section, "suppression_probability", config.suppression_probability);
        config.suppression_degrade_threshold =
            ConfigDouble(*section, "suppression_degrade_threshold", config.suppression_degrade_threshold);
        config.min_ticks = ConfigUint64(*section, "min_ticks", config.min_ticks);
        config.max_ticks = ConfigUint64(*section, "max_ticks", config.max_ticks);
    } else {
        config.suppression_threshold =
            ConfigDouble(*section, "contact_loss_suppression_threshold", config.suppression_threshold);
        config.damage_probability =
            ConfigDouble(*section, "contact_loss_damage_probability", config.damage_probability);
        config.suppression_probability =
            ConfigDouble(*section, "contact_loss_suppression_probability", config.suppression_probability);
        config.min_ticks = ConfigUint64(*section, "contact_loss_min_ticks", config.min_ticks);
        config.max_ticks = ConfigUint64(*section, "contact_loss_max_ticks", config.max_ticks);
    }
    if (!config.is_valid()) {
        throw std::invalid_argument("contact 配置非法（概率须在 [0,1]，max_ticks >= min_ticks）");
    }
    return config;
}

ContactLossResult resolve_contact_loss(const ContactLossInput& input, const ContactConfig& config, Rng& rng) {
    ContactLossResult result;
    if (!input.hit) {
        return result;
    }
    const double damage_probability = config.damage_probability * Clamp01(input.damage_ratio);
    const double suppression_probability = input.suppression >= config.suppression_threshold
                                               ? (config.suppression_probability * Clamp01(input.suppression))
                                               : 0.0;
    result.probability = Clamp01(damage_probability + suppression_probability);
    const std::uint32_t roll = rng.next_bounded(kProbabilityScale);
    if (roll < static_cast<std::uint32_t>(result.probability * static_cast<double>(kProbabilityScale))) {
        result.lost = true;
        // M7：恢复时长闭区间 [min, max]（60–180s 契约含端点）。
        std::uint64_t span = config.max_ticks - config.min_ticks;
        if (span < kMaxBoundedSpan) {
            ++span;
        }
        result.duration_ticks =
            config.min_ticks + static_cast<std::uint64_t>(rng.next_bounded(static_cast<std::uint32_t>(span)));
    }
    return result;
}

void to_json(nlohmann::json& json, const ContactLossResult& result) {
    json = nlohmann::json{
        {"duration_ticks", result.duration_ticks}, {"lost", result.lost}, {"probability", result.probability}};
}

LastKnownState capture_last_known(const RuntimeUnitState& unit) {
    return LastKnownState{unit.x, unit.y, unit.suppression, unit.formation, unit.vehicle_modules, unit.destroyed, true};
}

void apply_last_known(RuntimeUnitState& unit, const LastKnownState& state) {
    if (!state.valid) {
        unit.has_last_known = false;
        return;
    }
    unit.last_known_x = state.x;
    unit.last_known_y = state.y;
    unit.last_known_suppression = state.suppression;
    unit.last_known_formation = state.formation;
    unit.last_known_modules = state.modules;
    unit.last_known_destroyed = state.destroyed;
    unit.has_last_known = true;
}

LastKnownState effective_unit_state(const RuntimeUnitState& unit) {
    if (unit.out_of_contact && unit.has_last_known) {
        return LastKnownState{unit.last_known_x,
                              unit.last_known_y,
                              unit.last_known_suppression,
                              unit.last_known_formation,
                              unit.last_known_modules,
                              unit.last_known_destroyed,
                              true};
    }
    return capture_last_known(unit);
}

std::string_view to_string(const EffectSeverity severity) noexcept {
    switch (severity) {
        case EffectSeverity::kNone:
            return "none";
        case EffectSeverity::kDegraded:
            return "degraded";
        case EffectSeverity::kDisabled:
            return "disabled";
    }
    return "unknown";
}

EffectSeverity effect_severity_from_string(const std::string_view name) {
    if (name == "none") {
        return EffectSeverity::kNone;
    }
    if (name == "degraded") {
        return EffectSeverity::kDegraded;
    }
    if (name == "disabled") {
        return EffectSeverity::kDisabled;
    }
    throw std::invalid_argument("未知复合状态严重度: " + std::string(name));
}

void to_json(nlohmann::json& json, const EffectSeverity severity) {
    json = to_string(severity);
}

void from_json(const nlohmann::json& json, EffectSeverity& severity) {
    severity = effect_severity_from_string(json.get<std::string>());
}

EffectSeverity worst_effect(const EffectSeverity lhs, const EffectSeverity rhs) noexcept {
    return lhs >= rhs ? lhs : rhs;
}

EffectSeverity effective_mobility_effect(const RuntimeUnitState& unit, const ContactConfig& config) noexcept {
    EffectSeverity result =
        unit.suppression >= config.suppression_degrade_threshold ? EffectSeverity::kDegraded : EffectSeverity::kNone;
    result = worst_effect(result, ModuleSeverity(unit.vehicle_modules.mobility));
    if (unit.destroyed) {
        result = EffectSeverity::kDisabled;
    }
    return result;
}

EffectSeverity effective_observation_effect(const RuntimeUnitState& unit, const ContactConfig& config) noexcept {
    if (unit.destroyed || unit.out_of_contact) {
        return EffectSeverity::kDisabled;
    }
    EffectSeverity result =
        unit.suppression >= config.suppression_degrade_threshold ? EffectSeverity::kDegraded : EffectSeverity::kNone;
    return worst_effect(result, ModuleSeverity(unit.vehicle_modules.optics));
}

EffectSeverity effective_command_effect(const RuntimeUnitState& unit, const ContactConfig& config) noexcept {
    if (unit.destroyed || unit.out_of_contact) {
        return EffectSeverity::kDisabled;
    }
    return unit.suppression >= config.suppression_degrade_threshold ? EffectSeverity::kDegraded : EffectSeverity::kNone;
}

EffectSeverity compound_effect(const RuntimeUnitState& unit, const ContactConfig& config) noexcept {
    return worst_effect(
        effective_mobility_effect(unit, config),
        worst_effect(effective_observation_effect(unit, config), effective_command_effect(unit, config)));
}

void step_contact(SimState& state) {
    for (RuntimeUnitState& unit : state.units) {
        if (unit.out_of_contact && unit.contact_ticks_remaining > 0U) {
            --unit.contact_ticks_remaining;
            if (unit.contact_ticks_remaining == 0U) {
                unit.out_of_contact = false;
                state.event_log.append(state.clock.tick(), EventCategory::kCombat, EventSeverity::kInfo,
                                       "CONTACT_RESTORED unit=" + unit.id);
            }
        }
    }
}

}  // namespace wfs::sim
