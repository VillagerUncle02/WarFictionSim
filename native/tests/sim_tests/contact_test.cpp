// tests/sim_tests/contact_test.cpp
//
// T032 失联机制测试（测试先行：RED → GREEN）。
//
// 覆盖（FR-065；宪法第 7 条）：
// - 失联概率统一 RNG：同种子两次结算逐字段一致，失联时长落在可配置的
//   60–180s 区间内；未命中不触发失联；
// - 最后已知状态：失联时捕获位置/状态快照，失联期间有效视图返回最后已知
//   状态，恢复后回到实际状态；
// - 压制/失联/模块损伤复合状态按"取最严"叠加；
// - 失联恢复按游戏 tick 倒计时并产生 CONTACT_RESTORED 事件；
// - step_sim_state 集成调用恢复逻辑。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/contact.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/model/combat.h"
#include "wfs/sim/rng.h"

namespace {

using wfs::sim::ContactConfig;
using wfs::sim::ContactLossInput;
using wfs::sim::ContactLossResult;
using wfs::sim::EffectSeverity;
using wfs::sim::GameClock;
using wfs::sim::LastKnownState;
using wfs::sim::load_scenario;
using wfs::sim::Rng;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_contact;
using wfs::sim::step_sim_state;
using wfs::sim::worst_effect;

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
    state.rng = Rng(seed, 0U);
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

// 构造必失联配置（probability = 1），用于时长区间与确定性断言。
ContactConfig AlwaysLostConfig() {
    ContactConfig config;
    config.damage_probability = 1.0;
    config.suppression_probability = 1.0;
    config.suppression_threshold = 0.0;
    return config;
}

}  // namespace

TEST(WfsContactTest, ContactLossDeterministicAndBounded) {
    const ContactConfig config = AlwaysLostConfig();
    Rng first(7U, 0U);
    Rng second(7U, 0U);
    const ContactLossResult a = wfs::sim::resolve_contact_loss(ContactLossInput{1.0, 0.4, true}, config, first);
    const ContactLossResult b = wfs::sim::resolve_contact_loss(ContactLossInput{1.0, 0.4, true}, config, second);
    EXPECT_EQ(nlohmann::json(a).dump(), nlohmann::json(b).dump()) << "同种子失联结算必须逐字段一致";
    ASSERT_TRUE(a.lost);
    EXPECT_GE(a.duration_ticks, config.min_ticks);
    EXPECT_LE(a.duration_ticks, config.max_ticks);
}

TEST(WfsContactTest, ContactLossRequiresHit) {
    const ContactConfig config = AlwaysLostConfig();
    Rng rng(7U, 0U);
    const ContactLossResult result = wfs::sim::resolve_contact_loss(ContactLossInput{1.0, 0.4, false}, config, rng);
    EXPECT_FALSE(result.lost);
    EXPECT_DOUBLE_EQ(result.probability, 0.0);
}

TEST(WfsContactTest, LastKnownStateCapturedAndEffectiveView) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->x = 1.0;
    unit->y = 1.0;
    unit->suppression = 0.3;

    const LastKnownState captured = wfs::sim::capture_last_known(*unit);
    ASSERT_TRUE(captured.valid);
    EXPECT_DOUBLE_EQ(captured.x, 1.0);
    EXPECT_DOUBLE_EQ(captured.y, 1.0);
    EXPECT_DOUBLE_EQ(captured.suppression, 0.3);

    wfs::sim::apply_last_known(*unit, captured);
    unit->out_of_contact = true;
    unit->x = 2.0;
    unit->y = 2.0;
    unit->suppression = 0.9;
    const LastKnownState effective = wfs::sim::effective_unit_state(*unit);
    EXPECT_DOUBLE_EQ(effective.x, 1.0) << "失联期间有效位置必须冻结为最后已知位置";
    EXPECT_DOUBLE_EQ(effective.y, 1.0);
    EXPECT_DOUBLE_EQ(effective.suppression, 0.3);

    unit->out_of_contact = false;
    const LastKnownState actual = wfs::sim::effective_unit_state(*unit);
    EXPECT_DOUBLE_EQ(actual.x, 2.0) << "恢复后有效视图必须回到实际状态";
    EXPECT_DOUBLE_EQ(actual.suppression, 0.9);
}

TEST(WfsContactTest, CompoundEffectsTakeStrictest) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);

    EXPECT_EQ(wfs::sim::effective_mobility_effect(*unit), EffectSeverity::kNone);
    EXPECT_EQ(wfs::sim::effective_command_effect(*unit), EffectSeverity::kNone);

    unit->suppression = 1.0;
    EXPECT_EQ(wfs::sim::effective_mobility_effect(*unit), EffectSeverity::kDegraded);
    EXPECT_EQ(wfs::sim::effective_command_effect(*unit), EffectSeverity::kDegraded);

    unit->out_of_contact = true;
    EXPECT_EQ(wfs::sim::effective_command_effect(*unit), EffectSeverity::kDisabled);
    EXPECT_EQ(wfs::sim::effective_observation_effect(*unit), EffectSeverity::kDisabled);
    EXPECT_EQ(wfs::sim::compound_effect(*unit), EffectSeverity::kDisabled);

    unit->vehicle_modules.mobility = wfs::sim::model::ModuleState::kDisabled;
    EXPECT_EQ(wfs::sim::effective_mobility_effect(*unit), EffectSeverity::kDisabled);
    EXPECT_EQ(worst_effect(EffectSeverity::kDisabled, EffectSeverity::kDegraded), EffectSeverity::kDisabled);
    EXPECT_EQ(worst_effect(EffectSeverity::kDegraded, EffectSeverity::kNone), EffectSeverity::kDegraded);
}

TEST(WfsContactTest, ContactRecoveryCountsDownAndRestores) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->out_of_contact = true;
    unit->contact_ticks_remaining = 3U;

    step_contact(state);
    EXPECT_TRUE(unit->out_of_contact);
    EXPECT_EQ(unit->contact_ticks_remaining, 2U);
    step_contact(state);
    step_contact(state);
    EXPECT_FALSE(unit->out_of_contact);
    EXPECT_TRUE(HasEvent(state.event_log, "CONTACT_RESTORED unit=squad-a"));
}

TEST(WfsContactTest, StepSimIntegratesContactRecovery) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->out_of_contact = true;
    unit->contact_ticks_remaining = 1U;
    step_sim_state(state);
    EXPECT_FALSE(FindUnit(state, "squad-a")->out_of_contact) << "step_sim_state 必须执行失联恢复";
}
