// tests/sim_tests/intel_sync_test.cpp
//
// T058：层级化情报同步单元测试（FR-028/029/031；宪法第 2/7 条）。
//
// 覆盖：连排 5s/营 15s 同步间隔、三类信息共用间隔（同一同步事件）、
// 直属可指挥单位实时同步时刻、来源标注重标（direct→sync→relay）、
// 子节点失联冻结最后已知状态、识别档位核心字段输出（T033 F5 登记项）。

#include <string>

#include <gtest/gtest.h>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/intel_sync.h"
#include "wfs/sim/loader.h"

namespace {

using wfs::sim::IntelRecord;
using wfs::sim::SimState;
using wfs::sim::find_intel;
using wfs::sim::load_scenario;
using wfs::sim::step_sim_state;

std::filesystem::path ScenarioPath() {
    return std::filesystem::path(WFS_SOURCE_ROOT) / "data" / "scenarios" / "scn-intel-roles.json";
}

SimState MakeState() {
    const auto load = load_scenario(ScenarioPath());
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().code + ": " +
                                                                 load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = wfs::sim::GameClock(load.scenario.tick_hz);
    state.rng = wfs::sim::Rng(7U, 0U);
    state.seed = 7U;
    state.threads = 1;
    state.scenario_path = ScenarioPath();
    wfs::sim::initialize_runtime_state(state);
    return state;
}

void Step(SimState& state, std::uint64_t ticks) {
    for (std::uint64_t i = 0U; i < ticks; ++i) {
        step_sim_state(state);
    }
}

bool HasEventPrefix(const SimState& state, const std::string& prefix) {
    for (const wfs::sim::SimEvent& event : state.event_log.events()) {
        if (event.message.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

std::string EventMessageAtTick(const SimState& state, const std::string& prefix, std::uint64_t tick) {
    for (const wfs::sim::SimEvent& event : state.event_log.events()) {
        if (event.tick == tick && event.message.starts_with(prefix)) {
            return event.message;
        }
    }
    return {};
}

TEST(WfsIntelSyncTest, HierarchySyncIntervalsAndSourceAnnotation) {
    SimState state = MakeState();
    Step(state, 100U);
    EXPECT_TRUE(HasEventPrefix(state, "INTEL_OBSERVED observer=plt-1-sq-1 target=enemy-sq-1 "));
    EXPECT_TRUE(HasEventPrefix(state, "INTEL_SYNC node=node-co-1 level=company tick=100 from=node-plt-1 "));
    EXPECT_FALSE(HasEventPrefix(state, "INTEL_SYNC node=node-bn-1 ")) << "营级不得在 15s 前同步";

    // 下级上报：连级视角的来源重标为 sync（具体发现单位 + 节点）。
    const IntelRecord* company = find_intel(state, "node-co-1", "enemy-sq-1");
    ASSERT_NE(company, nullptr);
    EXPECT_EQ(company->source.kind, "sync");
    EXPECT_EQ(company->source.unit_id, "plt-1-sq-1");
    EXPECT_EQ(company->source.node_id, "node-plt-1");

    // T033 F5 登记项：识别档位核心字段随记录输出。
    const IntelRecord* platoon = find_intel(state, "node-plt-1", "enemy-sq-1");
    ASSERT_NE(platoon, nullptr);
    EXPECT_GT(platoon->observed_count, 0U);
    EXPECT_EQ(platoon->type_name, "squad-rifle-opposition");
    EXPECT_FALSE(platoon->composition.empty());

    Step(state, 200U);  // tick=300：营级首次同步（15s）。
    EXPECT_TRUE(HasEventPrefix(state, "INTEL_SYNC node=node-bn-1 level=battalion tick=300 from=node-co-1 "));
    const IntelRecord* battalion = find_intel(state, "node-bn-1", "enemy-sq-1");
    ASSERT_NE(battalion, nullptr);
    EXPECT_EQ(battalion->source.kind, "relay");
    EXPECT_EQ(battalion->source.level, "company");
    EXPECT_TRUE(battalion->source.unit_id.empty()) << "更上级转发不得标注具体单位";
    EXPECT_TRUE(battalion->source.node_id.empty()) << "更上级转发只标来源层级";
}

TEST(WfsIntelSyncTest, DirectUnitsRealtimeSyncTicks) {
    SimState state = MakeState();
    Step(state, 37U);
    EXPECT_EQ(state.intel_sync_state.last_direct_sync_tick.at("node-plt-1"), 37U);
    EXPECT_EQ(state.intel_sync_state.last_direct_sync_tick.at("node-co-1"), 37U);
    EXPECT_EQ(state.intel_sync_state.last_direct_sync_tick.at("node-bn-1"), 37U);
    EXPECT_EQ(state.intel_sync_state.last_direct_sync_tick.count("node-enemy-1"), 1U);
}

TEST(WfsIntelSyncTest, ChildContactLossFreezesLastKnown) {
    SimState state = MakeState();
    Step(state, 100U);
    for (wfs::sim::RuntimeUnitState& unit : state.units) {
        if (unit.node_id == "node-plt-1") {
            unit.out_of_contact = true;
            unit.contact_ticks_remaining = 1000U;
        }
    }
    Step(state, 100U);  // tick=200：连级下一次同步，子节点失联。
    const std::string sync_event =
        EventMessageAtTick(state, "INTEL_SYNC node=node-co-1 level=company tick=200 from=node-plt-1 ", 200U);
    EXPECT_FALSE(sync_event.empty());
    EXPECT_NE(sync_event.find("last_known=2"), std::string::npos);
    EXPECT_NE(sync_event.find("enemy=0"), std::string::npos) << "失联子节点不得合并新敌情";

    // 连级视角的敌情不得被失联子节点刷新：last_seen 仍停留在首次同步时刻。
    const IntelRecord* company = find_intel(state, "node-co-1", "enemy-sq-1");
    ASSERT_NE(company, nullptr);
    EXPECT_EQ(company->last_seen_tick, 100U);
}

}  // namespace
