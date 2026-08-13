// tests/sim_tests/intel_test.cpp
//
// T033 迷雾/情报识别系统测试（测试先行：RED → GREEN）。
//
// 覆盖（FR-033/034/035；SC-006；宪法第 7 条）：
// - 可视距离与观察能力：观察评分决定可见性，识别分档 T1–T3 阈值数据驱动；
// - 记忆保留：完全脱离视野后按 memory_ticks 保留识别档位，超时丢失；
// - 最后动向：连续观察更新归一化运动方向；
// - 情报来源标注与过期：直属发现标注 direct，来源过期后标注失效但记忆保留。

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "state_serialization.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/model/combat.h"

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
    const ObservationResult far = wfs::sim::resolve_observation(
        ObservationInput{0.0, 0.0, 1.0, 10.0, 10.0, 0.0, false, false, 1.0, false}, config);
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
    EXPECT_EQ(record->source.node_id, "platoon-alpha");
    EXPECT_FALSE(record->source.unit_id.empty()) << "直属发现必须标注来源单位";

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

TEST(WfsIntelTest, LegacyRecordSerializationOmitsDefaultF5Fields) {
    // S3：T033 F5 三字段空值省略（与 IntelSource.level 同策略），旧存档
    // 加载后再次序列化字节一致（宪法第 13 条）。
    IntelRecord record;
    record.observer_node_id = "node-a";
    record.target_unit_id = "squad-b";
    record.tier = RecognitionTier::kT1;
    record.last_seen_tick = 5U;
    record.memory_until_tick = 100U;
    record.source_expires_tick = 50U;
    record.source = wfs::sim::IntelSource{"direct", "unit-a", "node-a", 5U, ""};
    record.last_known_x = 1.0;
    record.last_known_y = 2.0;

    const nlohmann::json json = record;
    EXPECT_FALSE(json.contains("observed_count"));
    EXPECT_FALSE(json.contains("type_name"));
    EXPECT_FALSE(json.contains("composition"));
    EXPECT_FALSE(json.at("source").contains("level"));

    // 旧存档（无三键）加载后再次序列化：字节必须一致（两次序列化相同）。
    const std::string first = json.dump();
    const IntelRecord restored = json.get<IntelRecord>();
    EXPECT_EQ(nlohmann::json(restored).dump(), first) << "旧存档加载后再次序列化必须逐字节一致";

    // 非缺省字段仍然输出：识别档位核心字段随快照可见。
    IntelRecord identified = record;
    identified.observed_count = 12U;
    identified.type_name = "squad-rifle-opposition";
    identified.composition = "rifle,mg";
    identified.source.level = "company";
    const nlohmann::json identified_json = identified;
    EXPECT_EQ(identified_json.at("observed_count").get<std::uint64_t>(), 12U);
    EXPECT_EQ(identified_json.at("type_name").get<std::string>(), "squad-rifle-opposition");
    EXPECT_EQ(identified_json.at("composition").get<std::string>(), "rifle,mg");
    EXPECT_EQ(identified_json.at("source").at("level").get<std::string>(), "company");
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

TEST(WfsIntelTest, OpticsDisabledPreventsIdentification) {
    // Code Reviewer M5（🟡）：观瞄模块失效必须阻断识别（data-model §6）。
    SimState state = MakeState();
    for (RuntimeUnitState& unit : state.units) {
        if (unit.node_id == "platoon-alpha") {
            unit.vehicle_modules.optics = wfs::sim::model::ModuleState::kDisabled;
        }
    }
    RuntimeUnitState* enemy = FindUnit(state, "squad-c");
    ASSERT_NE(enemy, nullptr);
    enemy->x = 1.05;
    enemy->y = 1.05;

    step_intel(state);
    EXPECT_EQ(wfs::sim::find_intel(state, "platoon-alpha", "squad-c"), nullptr) << "观瞄失效不得产生识别记录";
}

TEST(WfsIntelTest, OpticsDegradedOrSuppressedLowersTier) {
    // Code Reviewer M5（🟡）：观瞄降级/压制必须按有效观察能力衰减识别档位。
    SimState degraded = MakeState();
    RuntimeUnitState* degraded_observer = FindUnit(degraded, "squad-a");
    ASSERT_NE(degraded_observer, nullptr);
    degraded_observer->vehicle_modules.optics = wfs::sim::model::ModuleState::kDegraded;
    RuntimeUnitState* degraded_enemy = FindUnit(degraded, "squad-c");
    degraded_enemy->x = 1.05;
    degraded_enemy->y = 1.05;
    step_intel(degraded);
    const wfs::sim::IntelRecord* degraded_record = wfs::sim::find_intel(degraded, "platoon-alpha", "squad-c");
    ASSERT_NE(degraded_record, nullptr);
    EXPECT_LT(degraded_record->tier, wfs::sim::RecognitionTier::kT3) << "观瞄降级应降低识别档位";

    SimState suppressed = MakeState();
    RuntimeUnitState* suppressed_observer = FindUnit(suppressed, "squad-a");
    ASSERT_NE(suppressed_observer, nullptr);
    suppressed_observer->suppression = 1.0;
    RuntimeUnitState* suppressed_enemy = FindUnit(suppressed, "squad-c");
    suppressed_enemy->x = 1.05;
    suppressed_enemy->y = 1.05;
    step_intel(suppressed);
    const wfs::sim::IntelRecord* suppressed_record = wfs::sim::find_intel(suppressed, "platoon-alpha", "squad-c");
    ASSERT_NE(suppressed_record, nullptr);
    EXPECT_LT(suppressed_record->tier, wfs::sim::RecognitionTier::kT3) << "压制应降低识别档位";
}

TEST(WfsIntelTest, LargeObservationPassIsDeterministicAndWithinBudget) {
    // Code Reviewer M6（🟡）：大规模观察 pass 必须保持确定性（消除每 tick
    // 地形索引重建/无序遍历影响），并给出宽松的耗时上限冒烟。
    const auto make_large_state = [] {
        SimState state = MakeState();
        for (std::size_t i = 0U; i < 196U; ++i) {
            RuntimeUnitState unit;
            unit.id = "filler-" + std::to_string(i);
            unit.type = "squad-rifle-us";
            unit.node_id = (i % 2U == 0U) ? "platoon-alpha" : "enemy-command";
            unit.x = 0.5 + (0.01 * static_cast<double>(i % 40U));
            unit.y = 0.5 + (0.01 * static_cast<double>(i / 40U));
            state.units.push_back(std::move(unit));
        }
        return state;
    };
    SimState first = make_large_state();
    SimState second = make_large_state();
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t tick = 0U; tick < 10U; ++tick) {
        step_intel(first);
        step_intel(second);
        first.clock.advance(1U);
        second.clock.advance(1U);
    }
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(wfs::sim::serialize_state_json(first).dump(), wfs::sim::serialize_state_json(second).dump())
        << "同一输入两次大规模观察必须逐字节一致";
    EXPECT_LT(elapsed_ms, 5000) << "200 单位规模下观察 pass 耗时应在预算内";
}
