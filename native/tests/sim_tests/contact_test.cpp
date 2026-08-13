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

// N2 回归辅助：固定 seed 下，恢复时长必须等于 64 位闭区间均匀采样的期望值
// 且落在 [min_ticks, max_ticks] 内。期望值由 PCG32 参考实现（threshold 拒绝
// 采样，64 位候选由两次 next() 拼成）离线推导，用于锁定确定性语义。
void ExpectClosedIntervalDuration(const ContactConfig& config, std::uint64_t seed, std::uint64_t want) {
    Rng rng(seed, 0U);
    const ContactLossResult result = wfs::sim::resolve_contact_loss(ContactLossInput{1.0, 1.0, true}, config, rng);
    ASSERT_TRUE(result.lost) << "seed=" << seed;
    EXPECT_EQ(result.duration_ticks, want) << "seed=" << seed;
    EXPECT_GE(result.duration_ticks, config.min_ticks) << "seed=" << seed;
    EXPECT_LE(result.duration_ticks, config.max_ticks) << "seed=" << seed;
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

TEST(WfsContactTest, RecoveryDurationCoversMaxTickInclusive) {
    // Code Reviewer M7（💭）：恢复时长抽样必须包含 max（60–180s 契约闭区间）。
    ContactConfig config = AlwaysLostConfig();
    config.min_ticks = 10U;
    config.max_ticks = 11U;
    bool saw_max = false;
    for (std::uint64_t seed = 1U; seed <= 100U && !saw_max; ++seed) {
        Rng rng(seed, 0U);
        const ContactLossResult result = wfs::sim::resolve_contact_loss(ContactLossInput{1.0, 1.0, true}, config, rng);
        ASSERT_TRUE(result.lost);
        EXPECT_GE(result.duration_ticks, config.min_ticks);
        EXPECT_LE(result.duration_ticks, config.max_ticks);
        saw_max = result.duration_ticks == config.max_ticks;
    }
    EXPECT_TRUE(saw_max) << "恢复时长必须能取到 max（旧实现抽样区间为 [min, max-1]）";
}

TEST(WfsContactTest, ContactRecoverySpanExactPowerOfTwoBoundary) {
    // N2 回归：span 恰为 2^32（2^32 ≡ 0 mod 2^32）时，旧实现经 uint32 截断后
    // next_bounded(0)=0 恒返回 min_ticks；统一 64 位采样必须覆盖整个闭区间。
    ContactConfig config = AlwaysLostConfig();
    config.min_ticks = 1U;
    config.max_ticks = (UINT64_C(1) << 32) + 1U;  // span = 2^32。
    ExpectClosedIntervalDuration(config, 1U, 3527372289U);
    ExpectClosedIntervalDuration(config, 2U, 2875648929U);
    ExpectClosedIntervalDuration(config, 3U, 1324313485U);
    ExpectClosedIntervalDuration(config, 7U, 597142934U);
    ExpectClosedIntervalDuration(config, 42U, 3555308143U);
}

TEST(WfsContactTest, ContactRecoveryLargeSpanBeyondUint32) {
    // N2 回归：span > UINT32_MAX 时旧实现 uint64→uint32 截断使分布坍缩到
    // 低位余数（span≡0 mod 2^32 时恒为 min）；64 位采样必须覆盖 [min, max]。
    ContactConfig config = AlwaysLostConfig();
    config.min_ticks = 1U;
    config.max_ticks = (UINT64_C(1) << 32) + 6U;  // span = 2^32 + 5 > UINT32_MAX。
    ExpectClosedIntervalDuration(config, 7U, 2766973830U);
    ExpectClosedIntervalDuration(config, 42U, 219078134U);
}

TEST(WfsContactTest, ContactRecoveryMinEqualsMaxIsStable) {
    // N2 回归：min==max（span=0，span+1=1）时闭区间抽样必须恒返回该唯一值，
    // 且不得因 64 位采样引入除零/推进 RNG 的差异。
    ContactConfig config = AlwaysLostConfig();
    config.min_ticks = 500U;
    config.max_ticks = 500U;
    for (std::uint64_t seed = 1U; seed <= 32U; ++seed) {
        ExpectClosedIntervalDuration(config, seed, 500U);
    }
}

TEST(WfsContactTest, ContactConfigRejectsOutOfRangeMaxTicks) {
    // N2 回归：is_valid 必须带上界校验（max_ticks 不超过 2^53-1，即 double
    // 安全整数上限），非法配置显式无效/报错，不静默（宪法 §12/§17）。
    constexpr std::uint64_t kMaxSafeTick = (UINT64_C(1) << 53) - 1U;
    ContactConfig upper_bound_ok = AlwaysLostConfig();
    upper_bound_ok.max_ticks = kMaxSafeTick;
    EXPECT_TRUE(upper_bound_ok.is_valid());

    ContactConfig beyond_upper_bound = AlwaysLostConfig();
    beyond_upper_bound.max_ticks = (UINT64_C(1) << 53);  // 2^53 越界。
    EXPECT_FALSE(beyond_upper_bound.is_valid());

    ContactConfig inverted = AlwaysLostConfig();
    inverted.min_ticks = 10U;
    inverted.max_ticks = 9U;
    EXPECT_FALSE(inverted.is_valid());

    EXPECT_THROW(
        ContactConfig::FromScenario(nlohmann::json{{"contact", nlohmann::json{{"max_ticks", UINT64_C(1) << 53}}}}),
        std::invalid_argument);
}

TEST(WfsContactTest, SuppressionDegradeThresholdIsDataDriven) {
    // Code Reviewer M9（💭）：压制降级阈值并入 ContactConfig 数据驱动。
    const ContactConfig config = ContactConfig::FromScenario(
        nlohmann::json{{"contact", nlohmann::json{{"suppression_degrade_threshold", 0.8}}}});
    EXPECT_DOUBLE_EQ(config.suppression_degrade_threshold, 0.8);

    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->suppression = 0.6;
    EXPECT_EQ(wfs::sim::effective_mobility_effect(*unit, config), EffectSeverity::kNone)
        << "压制 0.6 低于数据阈值 0.8 不得降级";
    EXPECT_EQ(wfs::sim::effective_command_effect(*unit, config), EffectSeverity::kNone);

    EXPECT_THROW(ContactConfig::FromScenario(
                     nlohmann::json{{"contact", nlohmann::json{{"suppression_degrade_threshold", 1.5}}}}),
                 std::invalid_argument);
}
