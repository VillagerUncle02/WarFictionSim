// sim/src/combat.cpp
//
// T031：战斗结算系统实现。
//
// 实现策略：
// - 全部结算为纯函数 + 显式 Rng：命中/伤亡/失联等随机性只来自调用方传入
//   的统一 PCG32 流，固定输入必然产生固定输出（宪法第 7 条）。
// - 数值公式为确定性分段基线（research.md §5 的"实现阶段默认基线"）：
//   命中率、穿深衰减、伤害倍率、压制增益、失联概率与时长全部可经
//   CombatConfig 覆盖（宪法第 12 条：默认基线以配置承载）。
// - 遍历顺序固定：班组士兵按名单顺序、候选目标按输入顺序参与评分排序
//   （排序并列时按距离/单位 id 二次键），不依赖任何无序容器（宪法第 7 条）。
// - 错误路径显式：空指针输入抛 std::invalid_argument（宪法第 17 条，
//   不静默吞错）；调用方（模拟主循环）负责把异常映射为事件/错误码。

#include "wfs/sim/combat.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/movement.h"

namespace wfs::sim {

namespace {

constexpr double kRollScale = 4294967296.0;         // 2^32：RNG 输出 → [0,1)。
constexpr std::uint32_t kProbabilityScale = 1000U;  // 概率判定统一比例尺。
constexpr double kMetersPerKilometer = 1000.0;
constexpr std::uint32_t kMaxBoundedSpan = 1000000U;     // next_bounded 上限护栏。
constexpr double kSmallArmsReferenceFootprintM = 15.0;  // 尺寸系数参考占地半径。
constexpr double kAreaRadiusScaleM = 20.0;              // 人员弹药半径评分参考。
constexpr double kHelmetArmorMm = 3.0;                  // FR-061 个人防护基线。
constexpr double kLightVestArmorMm = 4.0;
constexpr double kHeavyVestArmorMm = 8.0;
constexpr double kVehicleUnscoredPenalty = 0.25;       // 未击穿弹药的评分惩罚系数。
constexpr double kChemicalDamageRamp = 0.5;            // 化学能伤害随差值增速。
constexpr double kKineticDamageLowRatio = 0.25;        // 基准伤害区间上限差值比。
constexpr double kKineticDamageRampRange = 0.75;       // 基准→峰值区间长度。
constexpr double kSevereDamageRatio = 0.6;             // 严重受损阈值（FR-062）。
constexpr double kAbandonProbability = 0.3;            // 严重受损弃车概率基线。
constexpr double kSurvivalDamageScale = 0.8;           // 摧毁时伤亡传递系数。
constexpr double kPersonalArmorProtectionScale = 0.1;  // 防弹衣对区域杀伤的减伤系数。

double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

// 掩蔽系数查表（FR-060）：与命中公式共用同一确定性映射。
double CoverFactor(const model::CoverState cover, const CombatConfig& config) {
    switch (cover) {
        case model::CoverState::kNone:
            return config.cover_none_factor;
        case model::CoverState::kPartial:
            return config.cover_partial_factor;
        case model::CoverState::kFull:
            return config.cover_full_factor;
    }
    return config.cover_none_factor;
}

// 弹药对装甲目标的有效性评分（FR-058/060）：击穿后按基准伤害与差值增长，
// 未击穿按穿深/装甲比例给低分，保证"最合适可用弹药"确定性排序。
double VehicleEffectiveness(const model::Ammo& ammo, double armor_mm, const CombatConfig& config) {
    const double pen = ammo.anti_armor.penetration_mm;
    const double base = ammo.anti_armor.base_damage;
    if (ammo.warhead_kind == model::WarheadKind::kSpecial) {
        return 0.0;  // 特种弹药（烟幕等）无对甲效果，仅作降级候选。
    }
    if (armor_mm <= 0.0) {
        return base;
    }
    if (pen >= armor_mm) {
        const double ratio = (pen - armor_mm) / armor_mm;
        const double multiplier = std::min(1.0 + (kChemicalDamageRamp * ratio), config.chemical_damage_cap);
        return base * multiplier;
    }
    return base * (pen / armor_mm) * kVehicleUnscoredPenalty;  // 未击穿：效果有限（明确降级）。
}

// 弹药对人员目标的有效性评分：杀伤力 × 半径覆盖（FR-058）。
double PersonnelEffectiveness(const model::Ammo& ammo) {
    if (ammo.warhead_kind == model::WarheadKind::kSpecial) {
        return 0.0;
    }
    const double radii = ammo.anti_personnel.blast_radius_m + ammo.anti_personnel.fragment_radius_m;
    return ammo.anti_personnel.lethality * (1.0 + (radii / kAreaRadiusScaleM));
}

// 个人防护等效装甲（FR-061）：头盔 3mm + 轻/重防弹衣 4/8mm。
double PersonalArmorMm(const model::Protection& protection) {
    double armor = protection.helmet ? kHelmetArmorMm : 0.0;
    switch (protection.body_armor) {
        case model::ArmorClass::kNone:
            break;
        case model::ArmorClass::kLight:
            armor += kLightVestArmorMm;
            break;
        case model::ArmorClass::kHeavy:
            armor += kHeavyVestArmorMm;
            break;
    }
    return armor;
}

}  // namespace

namespace {

// 可覆盖配置字段的确定性读取：缺省回退；非法值（非有限/负数/零 tick）显式
// 抛错（与 MovementConfig 同级，宪法第 17 条，F9）。
double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("combat 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("combat 配置必须为正: ") + key);
    }
    return value;
}

}  // namespace

CombatConfig CombatConfig::Defaults() {
    return CombatConfig{};
}

CombatConfig CombatConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    CombatConfig config;
    if (!raw.contains("combat") || !raw["combat"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["combat"];
    config.moving_target_factor = ConfigDouble(json, "moving_target_factor", config.moving_target_factor);
    config.march_formation_factor = ConfigDouble(json, "march_formation_factor", config.march_formation_factor);
    config.combat_formation_factor = ConfigDouble(json, "combat_formation_factor", config.combat_formation_factor);
    config.cover_none_factor = ConfigDouble(json, "cover_none_factor", config.cover_none_factor);
    config.cover_partial_factor = ConfigDouble(json, "cover_partial_factor", config.cover_partial_factor);
    config.cover_full_factor = ConfigDouble(json, "cover_full_factor", config.cover_full_factor);
    config.suppressed_accuracy_penalty =
        ConfigDouble(json, "suppressed_accuracy_penalty", config.suppressed_accuracy_penalty);
    config.smoke_hit_reduction = ConfigDouble(json, "smoke_hit_reduction", config.smoke_hit_reduction);
    config.kinetic_range_decay = ConfigDouble(json, "kinetic_range_decay", config.kinetic_range_decay);
    config.kinetic_range_decay_distance_m =
        ConfigDouble(json, "kinetic_range_decay_distance_m", config.kinetic_range_decay_distance_m);
    config.kinetic_damage_peak = ConfigDouble(json, "kinetic_damage_peak", config.kinetic_damage_peak);
    config.kinetic_overmatch_ratio = ConfigDouble(json, "kinetic_overmatch_ratio", config.kinetic_overmatch_ratio);
    config.kinetic_overmatch_decay = ConfigDouble(json, "kinetic_overmatch_decay", config.kinetic_overmatch_decay);
    config.kinetic_overmatch_floor = ConfigDouble(json, "kinetic_overmatch_floor", config.kinetic_overmatch_floor);
    config.chemical_damage_cap = ConfigDouble(json, "chemical_damage_cap", config.chemical_damage_cap);
    config.suppression_hit_gain = ConfigDouble(json, "suppression_hit_gain", config.suppression_hit_gain);
    config.suppression_lethality_scale =
        ConfigDouble(json, "suppression_lethality_scale", config.suppression_lethality_scale);
    config.suppression_area_gain = ConfigDouble(json, "suppression_area_gain", config.suppression_area_gain);
    config.suppression_recovery_per_tick =
        ConfigDouble(json, "suppression_recovery_per_tick", config.suppression_recovery_per_tick);
    config.contact_loss_suppression_threshold =
        ConfigDouble(json, "contact_loss_suppression_threshold", config.contact_loss_suppression_threshold);
    config.contact_loss_damage_probability =
        ConfigDouble(json, "contact_loss_damage_probability", config.contact_loss_damage_probability);
    config.contact_loss_suppression_probability =
        ConfigDouble(json, "contact_loss_suppression_probability", config.contact_loss_suppression_probability);
    config.contact_loss_min_ticks = ConfigUint64(json, "contact_loss_min_ticks", config.contact_loss_min_ticks);
    config.contact_loss_max_ticks = ConfigUint64(json, "contact_loss_max_ticks", config.contact_loss_max_ticks);
    if (config.contact_loss_max_ticks < config.contact_loss_min_ticks) {
        throw std::invalid_argument("combat 配置非法（contact_loss_max_ticks 必须 >= min）");
    }
    config.fire_cooldown_ticks = ConfigUint64(json, "fire_cooldown_ticks", config.fire_cooldown_ticks);
    config.aggressive_threat_weight = ConfigDouble(json, "aggressive_threat_weight", config.aggressive_threat_weight);
    config.aggressive_distance_weight =
        ConfigDouble(json, "aggressive_distance_weight", config.aggressive_distance_weight);
    config.cautious_threat_weight = ConfigDouble(json, "cautious_threat_weight", config.cautious_threat_weight);
    config.cautious_distance_weight = ConfigDouble(json, "cautious_distance_weight", config.cautious_distance_weight);
    config.ammo_fit_weight = ConfigDouble(json, "ammo_fit_weight", config.ammo_fit_weight);
    config.smoke_radius_m = ConfigDouble(json, "smoke_radius_m", config.smoke_radius_m);
    return config;
}

std::string_view to_string(const HitDirection direction) noexcept {
    switch (direction) {
        case HitDirection::kFront:
            return "front";
        case HitDirection::kSide:
            return "side";
        case HitDirection::kTop:
            return "top";
        case HitDirection::kBottom:
            return "bottom";
    }
    return "unknown";
}

HitDirection hit_direction_from_string(const std::string_view name) {
    if (name == "front") {
        return HitDirection::kFront;
    }
    if (name == "side") {
        return HitDirection::kSide;
    }
    if (name == "top") {
        return HitDirection::kTop;
    }
    if (name == "bottom") {
        return HitDirection::kBottom;
    }
    throw std::invalid_argument("未知命中方向: " + std::string(name));
}

void to_json(nlohmann::json& json, const HitDirection direction) {
    json = to_string(direction);
}

void from_json(const nlohmann::json& json, HitDirection& direction) {
    direction = hit_direction_from_string(json.get<std::string>());
}

HitResult resolve_hit(const HitInput& input, const CombatConfig& config, Rng& rng) {
    if (input.weapon == nullptr || input.ammo == nullptr) {
        throw std::invalid_argument("resolve_hit: weapon/ammo 为空");
    }
    const double range_factor =
        input.weapon->effective_range_m > 0.0 ? Clamp01(1.0 - (input.range_m / input.weapon->effective_range_m)) : 0.0;
    const double size_factor = std::clamp(input.target_footprint_radius_m / kSmallArmsReferenceFootprintM, 0.5, 1.5);
    const double cover_factor = CoverFactor(input.target_cover, config);
    const double formation_factor = input.target_formation == model::Formation::kCombat ? config.combat_formation_factor
                                                                                        : config.march_formation_factor;
    const double move_factor = input.target_moving ? config.moving_target_factor : 1.0;
    const double shooter_factor = 1.0 - (input.shooter_suppression * config.suppressed_accuracy_penalty);
    const double experience_factor = 0.9 + (0.2 * Clamp01(input.shooter_experience));
    const double smoke_factor = 1.0 - (config.smoke_hit_reduction * Clamp01(input.target_smoke_concealment));

    const double hit_chance =
        Clamp01(input.weapon->accuracy * range_factor * size_factor * cover_factor * formation_factor * move_factor *
                shooter_factor * experience_factor * input.environment_accuracy_multiplier * smoke_factor);
    const double roll = static_cast<double>(rng.next()) / kRollScale;
    return HitResult{roll < hit_chance, hit_chance, roll};
}

DamageResult resolve_damage(const DamageInput& input, const CombatConfig& config) {
    if (input.ammo == nullptr) {
        throw std::invalid_argument("resolve_damage: ammo 为空");
    }
    DamageResult result;
    const model::Ammo& ammo = *input.ammo;
    double armor = 0.0;
    if (input.armor != nullptr) {
        const model::DirectionalArmor* direction = nullptr;
        switch (input.direction) {
            case HitDirection::kFront:
                direction = &input.armor->front;
                break;
            case HitDirection::kSide:
                direction = &input.armor->side;
                break;
            case HitDirection::kTop:
                direction = &input.armor->top;
                break;
            case HitDirection::kBottom:
                direction = &input.armor->bottom;
                break;
        }
        armor = ammo.warhead_kind == model::WarheadKind::kChemical ? direction->chemical_mm : direction->kinetic_mm;
    }
    result.armor_mm = armor;

    // 动能穿深随距离衰减，化学能穿深固定（FR-056）。
    double penetration = ammo.anti_armor.penetration_mm;
    if (ammo.warhead_kind == model::WarheadKind::kKinetic && config.kinetic_range_decay_distance_m > 0.0) {
        const double travel = std::min(1.0, input.range_m / config.kinetic_range_decay_distance_m);
        penetration *= std::max(0.0, 1.0 - (config.kinetic_range_decay * travel));
    }
    result.effective_penetration_mm = penetration;

    // 击穿判定：穿深大于防护厚度即击穿，击穿后才造成伤害（FR-056）。
    if (ammo.warhead_kind != model::WarheadKind::kSpecial && penetration > armor) {
        result.penetrated = true;
    } else {
        result.note = ammo.warhead_kind == model::WarheadKind::kSpecial ? "特种弹药无对甲效果" : "未击穿";
        return result;
    }

    const double base = ammo.anti_armor.base_damage;
    if (armor <= 0.0) {
        result.damage_multiplier = 1.0;
        result.damage = base;
        return result;
    }

    const double ratio = (penetration - armor) / armor;
    double multiplier = 1.0;
    if (ammo.warhead_kind == model::WarheadKind::kKinetic) {
        // 差值达到最低值造成基准伤害；差值越大伤害越大；差值过大指数衰减
        // （过穿，FR-057）。
        if (ratio < kKineticDamageLowRatio) {
            multiplier = 1.0;
        } else if (ratio <= 1.0) {
            multiplier =
                1.0 + ((config.kinetic_damage_peak - 1.0) * (ratio - kKineticDamageLowRatio) / kKineticDamageRampRange);
        } else {
            multiplier = config.kinetic_damage_peak * std::exp(-((ratio - 1.0) * config.kinetic_overmatch_decay));
            multiplier = std::max(multiplier, config.kinetic_overmatch_floor);
            if (ratio > config.kinetic_overmatch_ratio) {
                result.overmatch = true;
                result.note = "过穿衰减";
            }
        }
    } else {
        // 化学能伤害增长有上限（FR-057）。
        multiplier = std::min(1.0 + (kChemicalDamageRamp * ratio), config.chemical_damage_cap);
        if (multiplier >= config.chemical_damage_cap) {
            result.capped = true;
            result.note = "化学能上限";
        }
    }
    result.damage_multiplier = multiplier;
    result.damage = base * multiplier;
    return result;
}

double vehicle_hp(const model::Vehicle& vehicle) {
    double max_kinetic = 0.0;
    double max_chemical = 0.0;
    for (const model::DirectionalArmor* armor :
         {&vehicle.armor.front, &vehicle.armor.side, &vehicle.armor.top, &vehicle.armor.bottom}) {
        max_kinetic = std::max(max_kinetic, armor->kinetic_mm);
        max_chemical = std::max(max_chemical, armor->chemical_mm);
    }
    return max_kinetic + max_chemical;
}

// 四坐标语义不同但类型相同，函数名已表达顺序（攻击方在前、目标在后）。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
HitDirection hit_direction(const double attacker_x, const double attacker_y, const double target_x,
                           const double target_y) {
    // 无朝向数据时按方位角分桶：|dx| 与 |dy| 比较 → 前/侧；俯射（攻击方
    // 高于目标）留待间接火力（T031 散布圈）接入时使用，当前返回侧/前。
    constexpr double kEpsilon = 1e-9;  // 同点判定容差。
    const double delta_x = target_x - attacker_x;
    const double delta_y = target_y - attacker_y;
    if (std::abs(delta_x) < kEpsilon && std::abs(delta_y) < kEpsilon) {
        return HitDirection::kSide;
    }
    return std::abs(delta_x) >= std::abs(delta_y) ? HitDirection::kSide : HitDirection::kFront;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

AreaEngagementResult resolve_area_engagement(const AreaEngagementInput& input, const CombatConfig& config, Rng& rng) {
    if (input.ammo == nullptr || input.squad == nullptr) {
        throw std::invalid_argument("resolve_area_engagement: ammo/squad 为空");
    }
    AreaEngagementResult result;
    const model::Squad& squad = *input.squad;
    const std::size_t count = squad.soldiers.size();
    if (count == 0U) {
        return result;
    }
    const double radius_sum = input.ammo->anti_personnel.blast_radius_m + input.ammo->anti_personnel.fragment_radius_m;
    const double footprint = std::max(1.0, squad.footprint_radius_m);
    const double coverage = Clamp01(radius_sum / (2.0 * footprint)) * CoverFactor(input.cover, config);
    result.coverage = coverage;

    // 命中人数：N × 覆盖率的随机舍入（确定性：小数部分与 RNG 比较）。
    const double exact = static_cast<double>(count) * coverage;
    const std::uint32_t floor_hits = static_cast<std::uint32_t>(std::floor(exact));
    const double fraction = exact - static_cast<double>(floor_hits);
    result.hit_count =
        floor_hits + (rng.next_bounded(kProbabilityScale) <
                              static_cast<std::uint32_t>(fraction * static_cast<double>(kProbabilityScale))
                          ? 1U
                          : 0U);
    if (result.hit_count > count) {
        result.hit_count = static_cast<std::uint32_t>(count);
    }

    // 逐士兵个人防护结算（FR-060/061）：未命中人数不受伤。
    std::uint32_t casualties = 0U;
    for (std::size_t i = 0U; i < count && i < result.hit_count; ++i) {
        const model::Soldier& soldier = squad.soldiers[i];
        SoldierOutcome outcome;
        outcome.soldier_id = soldier.id;
        outcome.armor_score = soldier.protection.armor_score();
        const double armor_mm = PersonalArmorMm(soldier.protection);
        const double armor_penetration = input.ammo->anti_armor.penetration_mm;
        outcome.armor_penetrated =
            input.ammo->warhead_kind == model::WarheadKind::kKinetic && armor_penetration > armor_mm;
        double kill_probability = 0.0;
        if (input.ammo->warhead_kind == model::WarheadKind::kKinetic) {
            kill_probability = outcome.armor_penetrated ? input.ammo->anti_personnel.lethality : 0.0;
        } else {
            kill_probability =
                input.ammo->anti_personnel.lethality * (1.0 - (kPersonalArmorProtectionScale * outcome.armor_score));
        }
        if (kill_probability > 0.0 &&
            rng.next_bounded(kProbabilityScale) <
                static_cast<std::uint32_t>(kill_probability * static_cast<double>(kProbabilityScale))) {
            outcome.casualty = true;
            ++casualties;
        } else if (input.ammo->anti_personnel.lethality > 0.0) {
            outcome.suppressed = true;  // 未致死命中 → 压制（FR-063）。
        }
        result.outcomes.push_back(std::move(outcome));
    }
    // 区域火力整体压制：按命中比例 × 杀伤力（FR-063）。
    result.suppression_added = config.suppression_area_gain * input.ammo->anti_personnel.lethality *
                               (static_cast<double>(result.hit_count) / static_cast<double>(count));
    return result;
}

SuppressionResult resolve_suppression(const SuppressionInput& input, const CombatConfig& config) {
    double added = 0.0;
    if (input.area_hit) {
        added = config.suppression_area_gain * input.hit_lethality * input.area_hit_ratio;
    } else {
        added = config.suppression_hit_gain + (config.suppression_lethality_scale * input.hit_lethality);
    }
    return SuppressionResult{added, Clamp01(input.current_suppression + added)};
}

// 旧 CombatConfig 适配重载（T032 后权威实现位于 contact.cpp）：把战斗配置
// 中的 contact_loss_* 字段映射到 ContactConfig，保证既有调用方（黄金测试/
// 外部 API）与统一失联实现使用同一公式与 RNG 语义。
ContactLossResult resolve_contact_loss(const ContactLossInput& input, const CombatConfig& config, Rng& rng) {
    ContactConfig contact;
    contact.suppression_threshold = config.contact_loss_suppression_threshold;
    contact.damage_probability = config.contact_loss_damage_probability;
    contact.suppression_probability = config.contact_loss_suppression_probability;
    contact.min_ticks = config.contact_loss_min_ticks;
    contact.max_ticks = config.contact_loss_max_ticks;
    return wfs::sim::resolve_contact_loss(input, contact, rng);
}

TargetSelectionResult select_target(const TargetSelectionInput& input, const CombatConfig& config) {
    TargetSelectionResult result;
    if (input.weapon == nullptr) {
        throw std::invalid_argument("select_target: weapon 为空");
    }
    double threat_weight = 1.0;
    double distance_weight = 1.0;
    if (input.policy == model::EngagementPolicy::kAggressive) {
        threat_weight = config.aggressive_threat_weight;
        distance_weight = config.aggressive_distance_weight;
    } else if (input.policy == model::EngagementPolicy::kCautious) {
        threat_weight = config.cautious_threat_weight;
        distance_weight = config.cautious_distance_weight;
    }

    struct Scored {
        TargetCandidate candidate;
        double score;
    };
    std::vector<Scored> scored;
    scored.reserve(input.candidates.size());
    for (const TargetCandidate& candidate : input.candidates) {
        const double range_factor = input.weapon->effective_range_m > 0.0
                                        ? std::max(0.0, 1.0 - (candidate.distance_m / input.weapon->effective_range_m))
                                        : 0.0;
        // 弹药适配（F6/FR-060）：可用弹药非空时，按 select_ammo 对该候选的
        // 有效性预评分折算为乘性因子（效果 100 归一化），ammo_fit_weight 可调。
        double ammo_fit_factor = 1.0;
        if (!input.available_ammo.empty()) {
            const AmmoSelectionResult choice =
                select_ammo(AmmoSelectionInput{input.weapon, input.available_ammo, candidate.target_is_vehicle,
                                               candidate.target_armor_mm, candidate.distance_m},
                            config);
            const double effectiveness_norm = std::min(1.0, choice.effectiveness / 100.0);
            ammo_fit_factor = 1.0 + (config.ammo_fit_weight * effectiveness_norm);
        }
        const double score = threat_weight * candidate.threat * distance_weight * range_factor * ammo_fit_factor;
        scored.push_back(Scored{candidate, score});
    }
    // 确定性排序：分数降序，并列按距离升序、单位 id 升序（无序遍历不可接受）。
    std::sort(scored.begin(), scored.end(), [](const Scored& lhs, const Scored& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.candidate.distance_m != rhs.candidate.distance_m) {
            return lhs.candidate.distance_m < rhs.candidate.distance_m;
        }
        return lhs.candidate.unit_id < rhs.candidate.unit_id;
    });
    for (const Scored& entry : scored) {
        result.ranked.push_back(entry.candidate);
    }
    if (!scored.empty()) {
        result.target_id = scored.front().candidate.unit_id;
        result.score = scored.front().score;
    }
    return result;
}

AmmoSelectionResult select_ammo(const AmmoSelectionInput& input, const CombatConfig& config) {
    AmmoSelectionResult result;
    if (input.weapon == nullptr) {
        throw std::invalid_argument("select_ammo: weapon 为空");
    }
    double best_score = -1.0;
    bool best_mismatch = false;
    for (const model::Ammo* ammo : input.available) {
        if (ammo == nullptr || !input.weapon->supports_ammo(ammo->id)) {
            continue;
        }
        double score = 0.0;
        bool mismatch = false;
        if (input.target_is_vehicle) {
            score = VehicleEffectiveness(*ammo, input.target_armor_mm, config);
            if (ammo->warhead_kind != model::WarheadKind::kSpecial &&
                ammo->anti_armor.penetration_mm < input.target_armor_mm) {
                mismatch = true;
            }
            if (ammo->warhead_kind == model::WarheadKind::kSpecial) {
                mismatch = true;
            }
        } else {
            score = PersonnelEffectiveness(*ammo);
            if (ammo->warhead_kind == model::WarheadKind::kSpecial) {
                mismatch = true;
            }
        }
        if (score > best_score) {
            best_score = score;
            result.ammo_id = ammo->id;
            result.effectiveness = score;
            best_mismatch = mismatch;
        }
    }
    result.mismatch = best_mismatch || best_score <= 0.0;
    if (result.mismatch) {
        result.note = "弹药不匹配/效果有限";
    }
    return result;
}

void to_json(nlohmann::json& json, const HitResult& result) {
    json = nlohmann::json{{"hit", result.hit}, {"hit_chance", result.hit_chance}, {"roll", result.roll}};
}

void to_json(nlohmann::json& json, const DamageResult& result) {
    json = nlohmann::json{{"penetrated", result.penetrated},
                          {"effective_penetration_mm", result.effective_penetration_mm},
                          {"armor_mm", result.armor_mm},
                          {"damage", result.damage},
                          {"damage_multiplier", result.damage_multiplier},
                          {"overmatch", result.overmatch},
                          {"capped", result.capped},
                          {"note", result.note}};
}

void to_json(nlohmann::json& json, const SoldierOutcome& outcome) {
    json = nlohmann::json{{"soldier_id", outcome.soldier_id},
                          {"casualty", outcome.casualty},
                          {"suppressed", outcome.suppressed},
                          {"armor_score", outcome.armor_score},
                          {"armor_penetrated", outcome.armor_penetrated}};
}

void to_json(nlohmann::json& json, const AreaEngagementResult& result) {
    json = nlohmann::json{{"coverage", result.coverage},
                          {"hit_count", result.hit_count},
                          {"outcomes", result.outcomes},
                          {"suppression_added", result.suppression_added}};
}

void to_json(nlohmann::json& json, const SuppressionResult& result) {
    json = nlohmann::json{{"added", result.added}, {"total", result.total}};
}

void to_json(nlohmann::json& json, const TargetCandidate& candidate) {
    json = nlohmann::json{{"unit_id", candidate.unit_id},
                          {"distance_m", candidate.distance_m},
                          {"threat", candidate.threat},
                          {"target_is_vehicle", candidate.target_is_vehicle},
                          {"target_armor_mm", candidate.target_armor_mm}};
}

void to_json(nlohmann::json& json, const TargetSelectionResult& result) {
    json = nlohmann::json{{"target_id", result.target_id}, {"score", result.score}, {"ranked", result.ranked}};
}

void to_json(nlohmann::json& json, const AmmoSelectionResult& result) {
    json = nlohmann::json{{"ammo_id", result.ammo_id},
                          {"effectiveness", result.effectiveness},
                          {"mismatch", result.mismatch},
                          {"note", result.note}};
}

namespace {

// 运行时单位 → 模型班组视图（区域结算复用个人防护公式）。
model::Squad RuntimeSquadView(const RuntimeUnitState& unit) {
    model::Squad squad;
    squad.id = unit.id;
    squad.name = unit.type;
    squad.soldiers = unit.soldiers;
    squad.footprint_radius_m = kSmallArmsReferenceFootprintM;
    squad.formation = unit.formation;
    squad.cover = unit.cover;
    return squad;
}

const model::Ammo* FindAmmo(const SimState& state, const std::string& ammo_id) {
    for (const model::Ammo& ammo : state.ammo_library) {
        if (ammo.id == ammo_id) {
            return &ammo;
        }
    }
    return nullptr;
}

double DistanceKm(const RuntimeUnitState& lhs, const RuntimeUnitState& rhs) {
    const double delta_x = lhs.x - rhs.x;
    const double delta_y = lhs.y - rhs.y;
    return std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
}

void LogCombat(SimState& state, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kCombat, severity, std::move(message));
}

// 载员/乘员弃车与伤亡结算（FR-062）：被摧毁时按最后一次命中伤害相对摧毁
// 阈值传递伤亡；严重受损时按概率弃车。乘员组与载员组分别转为两个徒步班组
// （FR-062：车组与搭乘班组），弃车后载具清空名单（F3）。
// 注意：push_back 可能使 vehicle 引用失效，日志字段在 push 前拷贝（F2）。
void SettleVehicleOccupants(SimState& state, RuntimeUnitState& vehicle, const double damage_ratio, Rng& rng) {
    const std::string vehicle_id = vehicle.id;
    if (vehicle.soldiers.empty()) {
        return;
    }
    std::vector<model::Soldier> crew_survivors;
    std::vector<model::Soldier> passenger_survivors;
    std::size_t casualties = 0U;
    const bool destroyed = vehicle.destroyed;
    const std::size_t crew_count = std::min(vehicle.crew_count, vehicle.soldiers.size());
    for (std::size_t i = 0U; i < vehicle.soldiers.size(); ++i) {
        model::Soldier& soldier = vehicle.soldiers[i];
        double survival_probability = 1.0 - (kSurvivalDamageScale * std::clamp(damage_ratio, 0.0, 1.0));
        if (!destroyed) {
            survival_probability = 1.0;  // 严重受损弃车：人员全部下车（基线）。
        }
        const std::uint32_t roll = rng.next_bounded(kProbabilityScale);
        if (roll < static_cast<std::uint32_t>((1.0 - survival_probability) * static_cast<double>(kProbabilityScale))) {
            soldier.status = model::SoldierStatus::kCasualty;
            ++casualties;
        } else {
            soldier.status = model::SoldierStatus::kNormal;
            if (i < crew_count) {
                crew_survivors.push_back(std::move(soldier));
            } else {
                passenger_survivors.push_back(std::move(soldier));
            }
        }
    }
    vehicle.soldiers.clear();  // 两种弃车路径都清空载具名单（F3）。

    // push_back 可能重分配 state.units 使 vehicle 引用失效：位置/归属先拷贝。
    const std::string node_id = vehicle.node_id;
    const double dismount_x = vehicle.x;
    const double dismount_y = vehicle.y;
    const auto add_dismounted_squad = [&state, &node_id, &vehicle_id, dismount_x, dismount_y](
                                          std::vector<model::Soldier>& survivors, const std::string& suffix) {
        if (survivors.empty()) {
            return;
        }
        RuntimeUnitState dismounted;
        dismounted.id = vehicle_id + suffix;  // 调用前已拷贝 vehicle_id（F2）。
        dismounted.type = "dismounted-squad";
        dismounted.node_id = node_id;
        dismounted.x = dismount_x;
        dismounted.y = dismount_y;
        dismounted.formation = model::Formation::kMarch;
        dismounted.soldiers = std::move(survivors);
        dismounted.crew_count = dismounted.soldiers.size();  // 徒步班组全员为车组成员/载员组。
        dismounted.amphibious = true;
        state.units.push_back(std::move(dismounted));
    };
    const std::size_t crew_survivor_count = crew_survivors.size();
    const std::size_t passenger_survivor_count = passenger_survivors.size();
    add_dismounted_squad(crew_survivors, "-dismounted-crew");
    add_dismounted_squad(passenger_survivors, "-dismounted-passengers");

    LogCombat(state, EventSeverity::kInfo,
              "COMBAT_ABANDONED unit=" + vehicle_id +
                  " survivors=" + std::to_string(crew_survivor_count + passenger_survivor_count) +
                  " casualties=" + std::to_string(casualties) + " crew=" + std::to_string(crew_survivor_count) +
                  " passengers=" + std::to_string(passenger_survivor_count));
}

// 载具模块损伤：按累计伤害阈值推进模块状态（FR-064，确定性 RNG 选模块）。
void ApplyVehicleModuleDamage(SimState& state, RuntimeUnitState& vehicle, Rng& rng) {
    const double ratio = vehicle.vehicle_hp > 0.0 ? vehicle.vehicle_damage / vehicle.vehicle_hp : 1.0;
    if (vehicle.destroyed) {
        return;
    }
    if (ratio >= 1.0) {
        vehicle.destroyed = true;
        vehicle.vehicle_modules.mobility = model::ModuleState::kDisabled;
        vehicle.vehicle_modules.optics = model::ModuleState::kDisabled;
        vehicle.vehicle_modules.reloading = model::ModuleState::kDisabled;
        LogCombat(state, EventSeverity::kCritical, "COMBAT_VEHICLE_DESTROYED unit=" + vehicle.id);
        return;
    }
    if (ratio >= kSevereDamageRatio) {
        LogCombat(state, EventSeverity::kWarning, "COMBAT_VEHICLE_SEVERE unit=" + vehicle.id);
        return;
    }
    constexpr double kModuleDamageRatio = 0.3;  // 模块损伤阈值（FR-064）。
    if (ratio >= kModuleDamageRatio) {
        const std::uint32_t pick = rng.next_bounded(3U);
        model::ModuleState* module = nullptr;
        std::string module_name;
        if (pick == 0U) {
            module = &vehicle.vehicle_modules.mobility;
            module_name = "mobility";
        } else if (pick == 1U) {
            module = &vehicle.vehicle_modules.optics;
            module_name = "optics";
        } else {
            module = &vehicle.vehicle_modules.reloading;
            module_name = "reloading";
        }
        *module = model::ModuleState::kDisabled;
        LogCombat(state, EventSeverity::kWarning, "COMBAT_MODULE_DAMAGE unit=" + vehicle.id + " module=" + module_name);
    }
}

}  // namespace

// 战斗结算本质上是固定顺序的逐 tick 事务（恢复→目标选择→选弹→命中→伤害→
// 压制→失联→弃车），拆分会破坏结算顺序的可读性与确定性审查，故保留单一入口。
// NOLINTBEGIN(readability-function-cognitive-complexity)
void step_combat(SimState& state) {
    const CombatConfig& config = state.combat_config;
    // 恢复阶段（先于攻击结算，固定顺序；FR-063/065 独立恢复）。
    for (RuntimeUnitState& unit : state.units) {
        if (unit.suppression > 0.0) {
            unit.suppression = std::max(0.0, unit.suppression - config.suppression_recovery_per_tick);
        }
        // 失联恢复由 T032 step_contact 独立结算（FR-065：恢复各自独立进行）。
    }

    const double environment_accuracy =
        state.scenario.raw.value("environment", nlohmann::json::object()).value("accuracy_multiplier", 1.0);
    const std::vector<RuntimeUnitState> original_units = state.units;  // 新增弃车班组不参与本 tick 射击。
    for (const RuntimeUnitState& attacker : original_units) {
        if (attacker.destroyed || attacker.out_of_contact || attacker.weapons.empty()) {
            continue;
        }
        if (state.clock.tick() < attacker.last_fire_tick + attacker.fire_cooldown_ticks) {
            continue;
        }
        std::vector<TargetCandidate> candidates;
        for (const RuntimeUnitState& target : original_units) {
            if (target.id == attacker.id || target.side == attacker.side || target.destroyed || target.out_of_contact) {
                continue;
            }
            const double distance_m = DistanceKm(attacker, target) * kMetersPerKilometer;
            const double threat = target.weapons.empty() ? 0.5 : 1.0;
            candidates.push_back(TargetCandidate{target.id, distance_m, threat, target.is_vehicle,
                                                 target.is_vehicle ? target.vehicle_armor.side.kinetic_mm : 0.0});
        }
        if (candidates.empty()) {
            continue;
        }

        RuntimeUnitState* attacker_mut = nullptr;
        for (RuntimeUnitState& unit : state.units) {
            if (unit.id == attacker.id) {
                attacker_mut = &unit;
                break;
            }
        }
        if (attacker_mut == nullptr) {
            continue;
        }

        // 自动选弹输入（F6）：先收集可用弹药，供目标评分中的弹药适配预评分。
        std::vector<const model::Ammo*> available;
        for (const auto& [ammo_id, count] : attacker.ammo) {
            if (count > 0U) {
                if (const model::Ammo* ammo = FindAmmo(state, ammo_id)) {
                    available.push_back(ammo);
                }
            }
        }
        if (available.empty()) {
            if (attacker_mut->last_exhausted_weapon.empty()) {
                attacker_mut->last_exhausted_weapon = "all";
                LogCombat(state, EventSeverity::kWarning, "AMMO_EXHAUSTED unit=" + attacker.id + " weapon=all");
            }
            continue;
        }

        for (const model::Weapon& weapon : attacker.weapons) {
            std::vector<TargetCandidate> in_range;
            for (const TargetCandidate& candidate : candidates) {
                if (candidate.distance_m <= weapon.effective_range_m) {
                    in_range.push_back(candidate);
                }
            }
            if (in_range.empty()) {
                continue;
            }
            const TargetSelectionResult selected =
                select_target(TargetSelectionInput{&weapon, attacker.x, attacker.y, in_range,
                                                   model::EngagementPolicy::kBalanced, available},
                              config);
            if (selected.target_id.empty()) {
                continue;
            }

            RuntimeUnitState* target_mut = nullptr;
            for (RuntimeUnitState& unit : state.units) {
                if (unit.id == selected.target_id) {
                    target_mut = &unit;
                    break;
                }
            }
            if (target_mut == nullptr) {
                continue;
            }
            const double range_m = DistanceKm(attacker, *target_mut) * kMetersPerKilometer;

            // 自动选弹（FR-058）：按目标类型与距离取最合适可用弹药。
            const double armor_mm = target_mut->is_vehicle ? target_mut->vehicle_armor.side.kinetic_mm : 0.0;
            const AmmoSelectionResult ammo_choice =
                select_ammo(AmmoSelectionInput{&weapon, available, target_mut->is_vehicle, armor_mm, range_m}, config);
            if (ammo_choice.ammo_id.empty()) {
                continue;
            }
            const model::Ammo* ammo = FindAmmo(state, ammo_choice.ammo_id);
            if (ammo == nullptr) {
                continue;
            }
            --attacker_mut->ammo[ammo->id];
            if (ammo_choice.mismatch) {
                LogCombat(state, EventSeverity::kWarning,
                          "AMMO_MISMATCH unit=" + attacker.id + " ammo=" + ammo->id + " note=" + ammo_choice.note);
            }

            const double smoke = smoke_concealment_at(state.smoke_areas, target_mut->x, target_mut->y);
            constexpr double kBaselineCombatExperience = 0.5;  // 无乘员单位基线。
            const double shooter_experience =
                attacker.soldiers.empty() ? kBaselineCombatExperience : attacker.soldiers.front().experience.combat;
            constexpr double kVehicleFootprintM = 4.0;  // 载具目标尺寸基线。
            const HitInput hit_input{&weapon,
                                     ammo,
                                     range_m,
                                     target_mut->formation,
                                     target_mut->cover,
                                     target_mut->moving,
                                     target_mut->is_vehicle ? kVehicleFootprintM : kSmallArmsReferenceFootprintM,
                                     smoke,
                                     attacker.suppression,
                                     shooter_experience,
                                     environment_accuracy};
            attacker_mut->last_fire_tick = state.clock.tick();  // F1：开火即落冷却，miss/hit 均覆盖。
            const HitResult hit = resolve_hit(hit_input, config, state.rng);
            if (!hit.hit) {
                LogCombat(state, EventSeverity::kInfo,
                          "COMBAT_MISS attacker=" + attacker.id + " target=" + target_mut->id +
                              " range_m=" + std::to_string(range_m) + " ammo=" + ammo->id);
                break;  // 本 tick 本武器已开火（冷却生效）。
            }
            LogCombat(state, EventSeverity::kInfo,
                      "COMBAT_HIT attacker=" + attacker.id + " target=" + target_mut->id +
                          " range_m=" + std::to_string(range_m) + " ammo=" + ammo->id);

            double damage_ratio = 0.0;
            if (target_mut->is_vehicle) {
                const HitDirection direction = hit_direction(attacker.x, attacker.y, target_mut->x, target_mut->y);
                const DamageResult damage = resolve_damage(
                    DamageInput{ammo, range_m, &target_mut->vehicle_armor, target_mut->vehicle_hp, direction}, config);
                if (damage.penetrated) {
                    target_mut->vehicle_damage += damage.damage;
                    damage_ratio =
                        target_mut->vehicle_hp > 0.0 ? std::min(1.0, damage.damage / target_mut->vehicle_hp) : 0.0;
                    LogCombat(state, EventSeverity::kInfo,
                              "COMBAT_DAMAGE target=" + target_mut->id + " damage=" + std::to_string(damage.damage) +
                                  " penetrated=" + std::to_string(damage.effective_penetration_mm) +
                                  "mm armor=" + std::to_string(damage.armor_mm) + "mm");
                } else {
                    LogCombat(state, EventSeverity::kInfo,
                              "COMBAT_BOUNCE target=" + target_mut->id + " note=" + damage.note);
                }
            } else {
                const model::Squad squad_view = RuntimeSquadView(*target_mut);
                const AreaEngagementResult area = resolve_area_engagement(
                    AreaEngagementInput{ammo, &squad_view, target_mut->cover, smoke}, config, state.rng);
                std::size_t casualty_count = 0U;
                for (const SoldierOutcome& outcome : area.outcomes) {
                    for (model::Soldier& soldier : target_mut->soldiers) {
                        if (soldier.id != outcome.soldier_id) {
                            continue;
                        }
                        if (outcome.casualty) {
                            soldier.status = model::SoldierStatus::kCasualty;
                            ++casualty_count;
                        } else if (outcome.suppressed && soldier.status != model::SoldierStatus::kCasualty) {
                            soldier.status = model::SoldierStatus::kSuppressed;
                        }
                    }
                }
                if (area.suppression_added > 0.0) {
                    // FR-063：实际施加的单位压制与日志 suppression_added 同口径，
                    // 按命中比例缩放（resolve_area_engagement 已按同一比例计算）。
                    const double area_hit_ratio =
                        static_cast<double>(area.hit_count) / static_cast<double>(squad_view.soldiers.size());
                    const SuppressionResult suppression = resolve_suppression(
                        SuppressionInput{target_mut->suppression, ammo->anti_personnel.lethality, true, area_hit_ratio},
                        config);
                    target_mut->suppression = suppression.total;
                }
                LogCombat(state, EventSeverity::kInfo,
                          "COMBAT_AREA_HIT target=" + target_mut->id + " hits=" + std::to_string(area.hit_count) +
                              " casualties=" + std::to_string(casualty_count) +
                              " suppression_added=" + std::to_string(area.suppression_added));
                damage_ratio = static_cast<double>(casualty_count) /
                               static_cast<double>(std::max<std::size_t>(1U, target_mut->soldiers.size()));
            }

            // 特种弹药：烟幕施放（FR-023/058）。
            if (ammo->special_effect) {
                SmokeArea smoke_area;
                const std::string smoke_id = "smoke-" + std::to_string(state.next_smoke_id++);
                smoke_area.id = smoke_id;
                smoke_area.x = target_mut->x;
                smoke_area.y = target_mut->y;
                smoke_area.radius_m = config.smoke_radius_m;
                smoke_area.ticks_remaining = state.movement_config.smoke_duration_ticks;
                state.smoke_areas.push_back(std::move(smoke_area));
                LogCombat(state, EventSeverity::kInfo,
                          "COMBAT_SMOKE_DEPLOYED area=" + smoke_id + " unit=" + target_mut->id);
            }

            // 失联结算（FR-065）：重损伤或压制达阈值有概率失联。
            const ContactLossResult contact = resolve_contact_loss(
                ContactLossInput{target_mut->suppression, damage_ratio, hit.hit}, state.contact_config, state.rng);
            if (contact.lost && !target_mut->out_of_contact) {
                // 失联瞬间冻结最后已知状态（FR-065：位置/状态转为最后已知）。
                const LastKnownState snapshot = capture_last_known(*target_mut);
                apply_last_known(*target_mut, snapshot);
                target_mut->out_of_contact = true;
                target_mut->contact_ticks_remaining = contact.duration_ticks;
                LogCombat(state, EventSeverity::kWarning,
                          "COMBAT_OUT_OF_CONTACT unit=" + target_mut->id +
                              " duration_ticks=" + std::to_string(contact.duration_ticks));
            }
            // 载具模块损伤与弃车（FR-062/064）：在烟幕/失联之后结算，避免
            // 新增弃车班组使指针失效（弃车班组不参与本 tick 射击）。
            if (target_mut->is_vehicle) {
                ApplyVehicleModuleDamage(state, *target_mut, state.rng);
                if (target_mut->destroyed) {
                    SettleVehicleOccupants(state, *target_mut, damage_ratio, state.rng);
                } else {
                    const double damage_ratio_to_hp =
                        target_mut->vehicle_damage / std::max(1.0, target_mut->vehicle_hp);
                    if (damage_ratio_to_hp >= kSevereDamageRatio &&
                        state.rng.next_bounded(kProbabilityScale) <
                            static_cast<std::uint32_t>(kAbandonProbability * static_cast<double>(kProbabilityScale))) {
                        SettleVehicleOccupants(state, *target_mut, damage_ratio, state.rng);
                    }
                }
            }
            break;  // 每 tick 每单位至多一个武器开火（冷却预算）。
        }
    }
}
// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace wfs::sim
