// sim/include/wfs/sim/combat.h
//
// T031：战斗结算系统公开接口。
//
// 设计契约（FR-054–065；data-model.md §5–8）：
// - 命中与效果分离（FR-060）：命中率由武器精度、距离、目标尺寸、隐蔽、
//   队形、移动状态、射手压制与经验、环境决定；命中与否由统一 RNG 判定。
// - 穿深/伤害查表（FR-056/057）：动能穿深随距离衰减、化学能穿深固定；
//   伤害随"穿深-防护"差值增长，动能差值过大触发过穿指数衰减，化学能
//   伤害增长有上限；伤害倍率采用确定性分段公式（基线可配置）。
// - 班组区域结算 → 个人防护衔接（FR-060/061）：覆盖率先定命中人数，再
//   逐士兵按个人尺度穿深（头盔/防弹衣）判定伤亡/压制，未命中不受伤。
// - 压制量化（FR-063）与失联概率（FR-065）：命中/区域火力产生压制值；
//   重损伤或压制达阈值后有概率失联，失联时长 60–180s（可配置），全部
//   使用统一 RNG，固定输入固定输出。
// - 自动目标选择（FR-060）：威胁 + 距离 + 弹药适配的确定性排序，接敌
//   策略调整权重；自动选弹（FR-058）按目标类型与可用弹药取最合适者，
//   不匹配时按最合适可用弹药降级开火并明确提示。
// - 载具结算（FR-062）：四方向防护、模块损伤、弃车与乘员/载员伤亡按
//   最后一次命中伤害相对摧毁阈值传递。

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/model/combat.h"
#include "wfs/sim/model/mission.h"
#include "wfs/sim/rng.h"

namespace wfs::sim {

struct SimState;  // 内部运行时状态；仅步进函数需要完整定义。

// 命中方向（FR-055：前/侧/上/底四方向防护）。
enum class HitDirection : std::uint8_t {
    kFront = 0,
    kSide = 1,
    kTop = 2,
    kBottom = 3,
};

std::string_view to_string(HitDirection direction) noexcept;
HitDirection hit_direction_from_string(std::string_view name);
void to_json(nlohmann::json& json, HitDirection direction);
void from_json(const nlohmann::json& json, HitDirection& direction);

// 战斗结算配置（默认基线；场景 raw["combat"] 可覆盖，宪法第 12 条）。
struct CombatConfig {
    // 命中（FR-060）。
    double moving_target_factor = 0.8;      // 移动目标命中系数。
    double march_formation_factor = 1.0;    // 行军队形（接敌准备差）。
    double combat_formation_factor = 0.75;  // 战斗队形（接敌准备好）。
    double cover_none_factor = 1.0;
    double cover_partial_factor = 0.6;
    double cover_full_factor = 0.35;
    double suppressed_accuracy_penalty = 0.5;  // 射手压制对精度降效。
    double smoke_hit_reduction = 0.7;          // 烟幕遮蔽对命中降效。
    // 穿深/伤害（FR-056/057）。
    double kinetic_range_decay = 0.5;                // 动能穿深随距离衰减比例。
    double kinetic_range_decay_distance_m = 1000.0;  // 衰减参考距离。
    double kinetic_damage_peak = 1.5;                // 动能伤害峰值倍率。
    double kinetic_overmatch_ratio = 2.0;            // 过穿衰减起始差值比。
    double kinetic_overmatch_decay = 0.35;           // 过穿指数衰减系数。
    double kinetic_overmatch_floor = 0.4;            // 过穿伤害下限倍率。
    double chemical_damage_cap = 2.0;                // 化学能伤害上限倍率。
    // 压制（FR-063）。
    double suppression_hit_gain = 0.35;           // 命中基础压制增益。
    double suppression_lethality_scale = 0.3;     // 杀伤力对压制增益的贡献。
    double suppression_area_gain = 0.25;          // 区域火力压制增益系数。
    double suppression_recovery_per_tick = 0.01;  // 每 tick 自然恢复。
    // 失联（FR-065）。
    double contact_loss_suppression_threshold = 0.8;
    double contact_loss_damage_probability = 0.15;
    double contact_loss_suppression_probability = 0.2;
    std::uint64_t contact_loss_min_ticks = 1200U;  // 60s。
    std::uint64_t contact_loss_max_ticks = 3600U;  // 180s。
    std::uint64_t fire_cooldown_ticks = 60U;       // 单位自动接敌开火冷却（T031）。
    // 目标选择（FR-060）。
    double aggressive_threat_weight = 1.5;
    double aggressive_distance_weight = 1.0;
    double cautious_threat_weight = 1.0;
    double cautious_distance_weight = 1.5;
    double ammo_fit_weight = 1.0;
    double smoke_radius_m = 30.0;  // 烟幕弹施放半径（FR-023/058）。

    static CombatConfig Defaults();
    static CombatConfig FromScenario(const nlohmann::json& raw);
};

// ---- 命中（FR-060）----

struct HitInput {
    const model::Weapon* weapon = nullptr;
    const model::Ammo* ammo = nullptr;
    double range_m = 0.0;
    model::Formation target_formation = model::Formation::kMarch;
    model::CoverState target_cover = model::CoverState::kNone;
    bool target_moving = false;
    double target_footprint_radius_m = 15.0;
    double target_smoke_concealment = 0.0;
    double shooter_suppression = 0.0;
    double shooter_experience = 0.5;
    double environment_accuracy_multiplier = 1.0;
};

struct HitResult {
    bool hit = false;
    double hit_chance = 0.0;
    double roll = 0.0;
};

HitResult resolve_hit(const HitInput& input, const CombatConfig& config, Rng& rng);

// ---- 穿深与伤害（FR-054/056/057）----

struct DamageInput {
    const model::Ammo* ammo = nullptr;
    double range_m = 0.0;
    const model::ArmorProfile* armor = nullptr;  // 载具四方向防护。
    double target_vehicle_hp = 0.0;              // 0 = 非载具目标。
    HitDirection direction = HitDirection::kSide;
};

struct DamageResult {
    bool penetrated = false;
    double effective_penetration_mm = 0.0;
    double armor_mm = 0.0;
    double damage = 0.0;
    double damage_multiplier = 0.0;
    bool overmatch = false;  // 动能过穿衰减触发（FR-057）。
    bool capped = false;     // 化学能伤害上限生效。
    std::string note;        // "未击穿" / "过穿衰减" / "化学能上限" 等。
};

DamageResult resolve_damage(const DamageInput& input, const CombatConfig& config);

// 载具结构强度（确定性派生：四方向动能/化学能最大值之和）。
double vehicle_hp(const model::Vehicle& vehicle);

// 命中方向（按攻击方与目标相对方位角分桶，纯几何确定性函数）。
HitDirection hit_direction(double attacker_x, double attacker_y, double target_x, double target_y);

// ---- 班组区域结算 → 个人防护（FR-060/061）----

struct AreaEngagementInput {
    const model::Ammo* ammo = nullptr;
    const model::Squad* squad = nullptr;
    model::CoverState cover = model::CoverState::kNone;
    double smoke_concealment = 0.0;
};

struct SoldierOutcome {
    std::string soldier_id;
    bool casualty = false;
    bool suppressed = false;
    double armor_score = 0.0;
    bool armor_penetrated = false;
};

struct AreaEngagementResult {
    double coverage = 0.0;
    std::uint32_t hit_count = 0U;
    std::vector<SoldierOutcome> outcomes;  // 按士兵名单顺序，仅命中者记录。
    double suppression_added = 0.0;
};

AreaEngagementResult resolve_area_engagement(const AreaEngagementInput& input, const CombatConfig& config, Rng& rng);

// ---- 压制（FR-063）----

struct SuppressionInput {
    double current_suppression = 0.0;
    double hit_lethality = 0.0;
    bool area_hit = false;
};

struct SuppressionResult {
    double added = 0.0;
    double total = 0.0;
};

SuppressionResult resolve_suppression(const SuppressionInput& input, const CombatConfig& config);

// ---- 失联（FR-065）----

struct ContactLossInput {
    double suppression = 0.0;
    double damage_ratio = 0.0;  // 本次/累计伤害相对摧毁阈值比例。
    bool hit = true;
};

struct ContactLossResult {
    bool lost = false;
    double probability = 0.0;
    std::uint64_t duration_ticks = 0U;
};

ContactLossResult resolve_contact_loss(const ContactLossInput& input, const CombatConfig& config, Rng& rng);

// ---- 目标选择（FR-060）----

struct TargetCandidate {
    std::string unit_id;
    double distance_m = 0.0;
    double threat = 0.0;             // 对自身的威胁程度（调用方按敌方武器/态势量化）。
    bool target_is_vehicle = false;  // 弹药适配评分输入（F6）。
    double target_armor_mm = 0.0;    // 目标装甲厚度（针对载具）。
};

struct TargetSelectionInput {
    const model::Weapon* weapon = nullptr;
    double attacker_x = 0.0;
    double attacker_y = 0.0;
    std::vector<TargetCandidate> candidates;
    model::EngagementPolicy policy = model::EngagementPolicy::kBalanced;
    // 可用弹药（F6）：非空时把 select_ammo 的有效性预评分纳入目标评分。
    std::vector<const model::Ammo*> available_ammo;
};

struct TargetSelectionResult {
    std::string target_id;  // 空 = 无可选目标。
    double score = 0.0;
    std::vector<TargetCandidate> ranked;  // 确定性排序（含原始字段）。
};

TargetSelectionResult select_target(const TargetSelectionInput& input, const CombatConfig& config);

// ---- 自动选弹（FR-058/060）----

struct AmmoSelectionInput {
    const model::Weapon* weapon = nullptr;
    std::vector<const model::Ammo*> available;  // 单位当前可用弹药。
    bool target_is_vehicle = false;
    double target_armor_mm = 0.0;
    double range_m = 0.0;
};

struct AmmoSelectionResult {
    std::string ammo_id;  // 空 = 无可用弹药。
    double effectiveness = 0.0;
    bool mismatch = false;  // 弹药不匹配/效果有限。
    std::string note;
};

AmmoSelectionResult select_ammo(const AmmoSelectionInput& input, const CombatConfig& config);

// 推进一个 tick 的自动接敌结算：目标选择、选弹、命中/伤害、压制/失联、
// 模块损伤与弃车（FR-060/062/063/065）。固定顺序保证确定性。
void step_combat(SimState& state);

// ---- 结果序列化（黄金测试/事件日志/存档复用）----

void to_json(nlohmann::json& json, const HitResult& result);
void to_json(nlohmann::json& json, const DamageResult& result);
void to_json(nlohmann::json& json, const SoldierOutcome& outcome);
void to_json(nlohmann::json& json, const AreaEngagementResult& result);
void to_json(nlohmann::json& json, const SuppressionResult& result);
void to_json(nlohmann::json& json, const ContactLossResult& result);
void to_json(nlohmann::json& json, const TargetCandidate& candidate);
void to_json(nlohmann::json& json, const TargetSelectionResult& result);
void to_json(nlohmann::json& json, const AmmoSelectionResult& result);

}  // namespace wfs::sim
