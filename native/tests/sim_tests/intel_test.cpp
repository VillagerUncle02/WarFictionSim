// tests/sim_tests/intel_test.cpp
//
// T033 迷雾/情报识别系统测试（测试先行：RED → GREEN）。
//
// 覆盖（FR-033/034/035；SC-006；宪法第 7 条）：
// - 可视距离与观察能力：观察评分决定可见性，识别分档 T1–T3 阈值数据驱动；
// - 记忆保留：完全脱离视野后按 memory_ticks 保留识别档位，超时丢失；
// - 最后动向：连续观察更新归一化运动方向；
// - 情报来源标注与过期：直属发现标注 direct，来源过期后标注失效但记忆保留。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/loader.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::IntelConfig;
using wfs::sim::IntelRecord;
using wfs::sim::load_scenario;
using wfs::sim::ObservationInput;
using wfs::sim::ObservationResult;
using wfs::sim::RecognitionTier;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_intel;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

SimState MakeState(std::uint64_t seed = 42U) {
    const auto load = load_scenario(SampleScenario());
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = wfs::sim::Rng(seed, 0U);
    state.seed = seed;
    state.threads = 1;
    state.scenario_path = SampleScenario();
    wfs::sim::initialize_runtime_state(state);
    return state;
}

RuntimeUnitState* FindUnit(SimState& state, const std::string& unit_id) {
    for (RuntimeUnitState& unit : state.units) {
        if (unit.id == unit_id) {
            return &unit;
        }
    }
    return nullptr;
}

bool HasEvent(const wfs::sim::EventLog& log, const std::string& prefix) {
    for (const wfs::sim::SimEvent& event : log.events()) {
        if (event.message.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(WfsIntelTest, ObservationRangeAndRecognitionTiers) {
    const IntelConfig config = IntelConfig::FromScenario(nlohmann::json::object());
    // 高观察能力、近距离、无隐蔽：完全识别（T3）。
    const ObservationResult close =
        wfs::sim::resolve_observation(ObservationInput{0.0, 0.0, 1.0, 0.2, 0.2, 0.0, false, false, 1.0, false}, config);
    EXPECT_TRUE(close.visible);
    EXPECT_EQ(wfs::sim::recognition_tier(close.score, config), RecognitionTier::kT3);

    // 超出可视距离：不可见。
    const ObservationResult far =
        wfs::sim::resolve_observation(ObservationInput{0.0, 0.0, 1.0, 10.0, 10.0, 0.0, false, false, 1.0, false}, config);
    EXPECT_FALSE(far.visible);

    // 低观察能力 + 高隐蔽：评分低于 T1 阈值，不可见。
    const ObservationResult weak =
        wfs::sim::resolve_observation(ObservationInput{0.0, 0.0, 0.1, 0.2, 0.2, 0.5, false, false, 1.0, false}, config);
    EXPECT_LT(weak.score, config.t1_threshold);
    EXPECT_FALSE(weak.visible);
}

TEST(WfsIntelTest, MemoryRetentionKeepsTierUntilExpiry) {
    const IntelConfig config = IntelConfig::FromScenario(nlohmann::json::object());
    IntelRecord record;
    record.observer_node_id = "platoon-alpha";
    record.target_unit_id = "squad-c";
    record.tier = RecognitionTier::kT2;
    record.memory_until_tick = 100U;
    record.source_expires_tick = 80U;

    EXPECT_EQ(wfs::sim::effective_tier(record, 50U, config), RecognitionTier::kT2);
    EXPECT_EQ(wfs::sim::effective_tier(record, 101U, config), RecognitionTier::kNone);
}

TEST(WfsIntelTest, StepIntelObservesRemembersAndLosesMemory) {
    SimState state = MakeState();
    RuntimeUnitState* enemy = FindUnit(state, "squad-c");
    ASSERT_NE(enemy, nullptr);
    enemy->x = 1.05;
    enemy->y = 1.05;  // 敌方单位进入观察距离。

    step_intel(state);
    const IntelRecord* record = wfs::sim::find_intel(state, "platoon-alpha", "squad-c");
    ASSERT_NE(record, nullptr);
    EXPECT_GE(record->tier, RecognitionTier::kT1);
    EXPECT_EQ(record->source.kind, "direct");
    EXPECT_EQ(record->source.unit_id, "squad-a");

    // 完全脱离视野：记忆保留期内信息仍在。
    enemy->x = 4.5;
    enemy->y = 4.5;
    state.clock.advance(5U);
    EXPECT_NE(wfs::sim::find_intel(state, "platoon-alpha", "squad-c"), nullptr) << "记忆保留期内不得丢失";

    // 超过记忆保留期：记录丢失。
    const std::uint64_t memory_until = record->memory_until_tick;
    state.clock.reset(memory_until + 1U);
    step_intel(state);
    EXPECT_EQ(wfs::sim::find_intel(state, "platoon-alpha", "squad-c"), nullptr);
    EXPECT_TRUE(HasEvent(state.event_log, "INTEL_MEMORY_LOST target=squad-c"));
}

TEST(WfsIntelTest, SourceAnnotationExpiresWhileMemoryRemains) {
    SimState state = MakeState();
    RuntimeUnitState* enemy = FindUnit(state, "squad-c");
    ASSERT_NE(enemy, nullptr);
    enemy->x = 1.05;
    enemy->y = 1.05;
    step_intel(state);

    // 目标离开视野并把来源过期时间设为本 tick：下一步只过期来源，不删记忆。
    enemy->x = 4.5;
    enemy->y = 4.5;
    IntelRecord* record = wfs::sim::find_intel_mutable(state, "platoon-alpha", "squad-c");
    ASSERT_NE(record, nullptr);
    record->source_expires_tick = state.clock.tick();
    state.clock.advance(1U);
    step_intel(state);

    const IntelRecord* after = wfs::sim::find_intel(state, "platoon-alpha", "squad-c");
    ASSERT_NE(after, nullptr) << "来源过期不得删除记忆";
    EXPECT_EQ(after->source.kind, "expired");
    EXPECT_TRUE(after->source.unit_id.empty());
    EXPECT_TRUE(HasEvent(state.event_log, "INTEL_SOURCE_EXPIRED target=squad-c"));
}

TEST(WfsIntelTest, LastMotionRecordedFromConsecutiveObservations) {
    SimState state = MakeState();
    RuntimeUnitState* enemy = FindUnit(state, "squad-c");
    ASSERT_NE(enemy, nullptr);
    enemy->x = 1.0;
    enemy->y = 1.0;
    step_intel(state);
    const IntelRecord* first = wfs::sim::find_intel(state, "platoon-alpha", "squad-c");
    ASSERT_NE(first, nullptr);
    EXPECT_DOUBLE_EQ(first->last_motion_dx, 0.0);

    enemy->x = 1.1;
    enemy->y = 1.0;
    state.clock.advance(1U);
    step_intel(state);
    const IntelRecord* second = wfs::sim::find_intel(state, "platoon-alpha", "squad-c");
    ASSERT_NE(second, nullptr);
    EXPECT_GT(second->last_motion_dx, 0.9) << "最后动向应记录归一化运动方向（东向）";
    EXPECT_LT(std::abs(second->last_motion_dy), 0.1);
}

