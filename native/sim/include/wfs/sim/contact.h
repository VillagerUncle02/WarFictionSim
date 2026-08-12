// sim/include/wfs/sim/contact.h
//
// T032：失联机制公开接口。
//
// 设计契约（FR-065；data-model.md §5/§6）：
// - 失联触发与概率全部来自统一 PCG32 RNG（宪法第 7 条）：重火力命中且造成
//   一定损伤、或压制达到阈值时有概率失联；未命中不触发。概率与恢复时长
//   （60–180s 基线）由 ContactConfig 数据承载（raw["contact"]；兼容旧
//   raw["combat"].contact_loss_* 键），恢复按游戏 tick 倒计时。
// - 最后已知状态：失联瞬间捕获位置/状态/队形/模块快照，失联期间有效视图
//   （effective_unit_state）返回最后已知状态，恢复后回到实际状态。
// - 复合状态（压制/失联/模块损伤）各自独立结算，效果取最严者叠加
//   （worst_effect / effective_*_effect），全部消除后单位完全恢复。

#pragma once

#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include "wfs/sim/model/combat.h"
#include "wfs/sim/rng.h"

namespace wfs::sim {

struct SimState;          // 内部运行时状态（sim_state.h）。
struct RuntimeUnitState;  // 内部运行时单位（sim_state.h）。

// 失联配置（FR-065；默认 60–180s @20Hz，场景 raw["contact"] 可覆盖）。
struct ContactConfig {
    double suppression_threshold = 0.8;          // 压制触发失联的阈值。
    double damage_probability = 0.15;            // 重损伤触发失联概率基线。
    double suppression_probability = 0.2;        // 压制触发失联概率基线。
    double suppression_degrade_threshold = 0.5;  // 复合状态压制降级阈值（M9）。
    std::uint64_t min_ticks = 1200U;             // 恢复最短时长（60s @20Hz）。
    std::uint64_t max_ticks = 3600U;             // 恢复最长时长（180s @20Hz）。

    // 恢复时长上界（N2）：取 2^53-1（IEEE-754 double 可无损表达的最大整数）。
    // 为什么：闭区间抽样需计算 span+1 = max_ticks-min_ticks+1；若 max_ticks
    // 无上界，极端场景配置可能溢出 uint64 且远超 FR-065 分钟级恢复语义。
    // 按宪法 §12/§17，非法配置必须显式无效/报错，不得静默产出失真分布。
    static constexpr std::uint64_t kMaxContactTicks = (UINT64_C(1) << 53) - 1U;

    // 读取 raw["contact"]；无该节时兼容 raw["combat"].contact_loss_* 旧键。
    // 非法值（概率越界/max<min/max 超过 kMaxContactTicks）显式抛
    // std::invalid_argument（宪法 17）。
    static ContactConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept {
        return suppression_threshold >= 0.0 && suppression_threshold <= 1.0 && damage_probability >= 0.0 &&
               damage_probability <= 1.0 && suppression_probability >= 0.0 && suppression_probability <= 1.0 &&
               suppression_degrade_threshold >= 0.0 && suppression_degrade_threshold <= 1.0 && max_ticks >= min_ticks &&
               max_ticks <= kMaxContactTicks;
    }
};

// 失联判定输入：命中与否、当前压制、本次伤害相对摧毁阈值比例。
struct ContactLossInput {
    double suppression = 0.0;
    double damage_ratio = 0.0;
    bool hit = true;
};

// 失联判定结果：是否失联、触发概率、恢复时长（tick）。
struct ContactLossResult {
    bool lost = false;
    double probability = 0.0;
    std::uint64_t duration_ticks = 0U;
};

// 确定性失联结算（统一 RNG；固定输入固定输出）。
ContactLossResult resolve_contact_loss(const ContactLossInput& input, const ContactConfig& config, Rng& rng);

void to_json(nlohmann::json& json, const ContactLossResult& result);

// 最后已知状态（FR-065）：失联瞬间冻结的单位视图。
struct LastKnownState {
    double x = 0.0;
    double y = 0.0;
    double suppression = 0.0;
    model::Formation formation = model::Formation::kMarch;
    model::ModuleStatus modules;
    bool destroyed = false;
    bool valid = false;  // 是否已捕获（从未目视/从未失联过则为 false）。

    bool operator==(const LastKnownState&) const = default;
};

// 捕获单位当前状态为最后已知快照（纯函数，不修改单位）。
LastKnownState capture_last_known(const RuntimeUnitState& unit);
// 把快照写入单位（has_last_known=true；state.valid=false 时清除记忆）。
void apply_last_known(RuntimeUnitState& unit, const LastKnownState& state);
// 有效视图：失联且存在记忆时返回最后已知状态，否则返回实际状态。
LastKnownState effective_unit_state(const RuntimeUnitState& unit);

// 复合状态严重度（FR-065：取最严者；none < degraded < disabled）。
enum class EffectSeverity : std::uint8_t {
    kNone = 0,
    kDegraded = 1,
    kDisabled = 2,
};

std::string_view to_string(EffectSeverity severity) noexcept;
EffectSeverity effect_severity_from_string(std::string_view name);
void to_json(nlohmann::json& json, EffectSeverity severity);
void from_json(const nlohmann::json& json, EffectSeverity& severity);

// 取最严叠加：kDisabled 压过 kDegraded，kDegraded 压过 kNone。
EffectSeverity worst_effect(EffectSeverity lhs, EffectSeverity rhs) noexcept;

// 压制/失联/模块损伤各自维度取最严后的有效效果（压制降级阈值数据驱动，M9）。
EffectSeverity effective_mobility_effect(const RuntimeUnitState& unit,
                                         const ContactConfig& config = ContactConfig{}) noexcept;
EffectSeverity effective_observation_effect(const RuntimeUnitState& unit,
                                            const ContactConfig& config = ContactConfig{}) noexcept;
EffectSeverity effective_command_effect(const RuntimeUnitState& unit,
                                        const ContactConfig& config = ContactConfig{}) noexcept;
// 三个维度的最严叠加（FR-065：复合状态取最严）。
EffectSeverity compound_effect(const RuntimeUnitState& unit, const ContactConfig& config = ContactConfig{}) noexcept;

// 推进一 tick 的失联恢复：倒计时接触恢复时长，到期产生 CONTACT_RESTORED。
void step_contact(SimState& state);

}  // namespace wfs::sim
