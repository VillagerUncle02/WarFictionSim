// sim/include/wfs/sim/recon_tasks.h
//
// T035：侦察类任务判定机制公开接口。
//
// 设计契约（FR-042；CHK064 实现阶段判定机制基线）：
// - HIDDEN_RECON：到达目标点后潜伏 hidden_hold_ticks 未被发现 → 完成；
//   被发现 → RECON_DETECTED 判失败（进入失败后处置）。
// - INFILTRATE_RECON：三阶段判定——潜入目标点 → 潜伏 → 返回撤离点；
//   任一阶段被发现即失败。
// - OBSERVATION_POST：持续观察周期，每周期登记视野内敌方情报并循环；
//   被压制超阈值判失败。
// - FIRE_RECON：按参数射击 fire_recon_rounds 轮完成；被压制无法撤离判
//   失败并自动上报请求支援（Edge Cases：被拖住时自动上报并请求支援）。
// - 全部随机性来自统一 RNG，判定参数数据驱动（raw["recon"]，宪法 7/12）。

#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

namespace wfs::sim {

struct SimState;  // 内部运行时状态（sim_state.h）。

// 侦察判定配置（场景 raw["recon"] 可覆盖）。
struct ReconConfig {
    double detection_base_probability = 0.02;      // 每 tick 被发现概率基线。
    double detection_range_km = 0.4;               // 敌方发现半径。
    double moving_detection_multiplier = 1.5;      // 移动时被发现概率放大。
    std::uint64_t hidden_hold_ticks = 600U;        // 隐蔽侦察潜伏时长。
    std::uint64_t infiltrate_hold_ticks = 600U;    // 渗透侦察潜伏时长。
    std::uint64_t observation_cycle_ticks = 600U;  // 观察哨单周期时长。
    std::uint32_t fire_recon_rounds = 3U;          // 火力侦察射击轮数。
    double failure_suppression_threshold = 0.8;    // 侦察失败压制阈值。

    static ReconConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept {
        return detection_base_probability >= 0.0 && detection_base_probability <= 1.0 && detection_range_km > 0.0 &&
               moving_detection_multiplier >= 1.0 && hidden_hold_ticks > 0U && infiltrate_hold_ticks > 0U &&
               observation_cycle_ticks > 0U && fire_recon_rounds > 0U && failure_suppression_threshold >= 0.0 &&
               failure_suppression_threshold <= 1.0;
    }
};

// 推进一 tick 的侦察任务判定（固定单位顺序；统一 RNG）。
void step_recon_tasks(SimState& state);

}  // namespace wfs::sim
