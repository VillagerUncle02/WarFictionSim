// tests/sim_tests/runtime_combat_test.cpp
//
// F1/F3/F5/F8/F9 运行期战斗与机动测试（测试先行：RED → GREEN）。
// 覆盖：
// - F1 开火冷却：命中率 0 时脱靶仍消耗冷却，开火间隔 ≥ fire_cooldown_ticks；
// - F5 步兵接敌 → 命中 → 压制 → 失联，以及载具击穿 → 模块损伤 → 摧毁/
//   严重受损弃车（生成乘员组/载员组两个徒步班组，FR-062）；
// - F3 弃车后载具不再保留乘员空壳（两种路径都清空）；
// - F8 两栖能力数据驱动：步兵重装备不可泅渡、载具按数据 amphibious 判定，
//   非两栖单位深水阻塞（UNIT_STUCK）；
// - F9 非法 combat 配置显式抛错。
//
// 场景使用 data/scenarios/scn-runtime-combat-test.json + data/units/vehicles.json
// （F5 车辆目录），全部为确定性固定种子运行（宪法第 7 条）。

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/combat.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/rng.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::inject_player_command;
using wfs::sim::load_scenario;
using wfs::sim::Rng;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_sim_state;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path RuntimeCombatScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-runtime-combat-test.json";
}

std::filesystem::path CommandSchema() {
    return RepoRoot() / "contracts" / "schemas" / "command.schema.json";
}

// 白盒状态：与 wfs_sim_create 等价的加载 + 运行期初始化。
SimState MakeState(std::uint64_t seed = 42U) {
    const auto load = load_scenario(RuntimeCombatScenario());
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = Rng(seed, 0U);
    state.seed = seed;
    state.threads = 1;
    state.scenario_path = RuntimeCombatScenario();
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

std::vector<wfs::sim::SimEvent> EventsWith(const wfs::sim::EventLog& log, const std::string& text) {
    std::vector<wfs::sim::SimEvent> matches;
    for (const wfs::sim::SimEvent& event : log.events()) {
        if (event.message.find(text) != std::string::npos) {
            matches.push_back(event);
        }
    }
    return matches;
}

std::string MoveCommand(const std::string& unit_id, double x, double y) {
    return R"({"schema_version": 1, "type": "MOVE", "target": {"kind": "unit", "ref": ")" + unit_id +
           R"("}, "completion": {"condition": "reach_point", "params": {"point": {"x": )" + std::to_string(x) +
           R"(, "y": )" + std::to_string(y) +
           R"(}}}, "intent": "测试移动", "behavior": {"engagement": "balanced"}, "priority": 1, "deadline": {"game_time": 20000}})";
}

}  // namespace

TEST(WfsRuntimeCombatTest, HitConsumesFireCooldown) {
    SimState state = MakeState();
    RuntimeUnitState* attacker = FindUnit(state, "squad-friend");
    ASSERT_NE(attacker, nullptr);
    ASSERT_FALSE(attacker->weapons.empty());
    attacker->weapons.front().accuracy = 0.0;  // 命中率 0：必脱靶。

    const std::uint64_t cooldown = state.combat_config.fire_cooldown_ticks;
    for (std::uint64_t i = 0U; i < cooldown; ++i) {
        step_sim_state(state);  // 冷却期内不射击（last_fire_tick 初始 0）。
    }
    const std::vector<wfs::sim::SimEvent> first_misses =
        EventsWith(state.event_log, "COMBAT_MISS attacker=squad-friend");
    ASSERT_EQ(first_misses.size(), 1U) << "开火应产生一次脱靶";
    ASSERT_EQ(FindUnit(state, "squad-friend")->last_fire_tick, cooldown);

    // 冷却期内不得再次开火（修复前每 tick 连射，此处会失败）。
    for (std::uint64_t i = 0U; i < cooldown - 1U; ++i) {
        step_sim_state(state);
    }
    EXPECT_EQ(EventsWith(state.event_log, "COMBAT_MISS attacker=squad-friend").size(), 1U) << "冷却期内不应再次开火";

    // 冷却到期后允许下一次开火：两次脱靶的 tick 间隔 ≥ fire_cooldown_ticks。
    step_sim_state(state);
    const std::vector<wfs::sim::SimEvent> second_misses =
        EventsWith(state.event_log, "COMBAT_MISS attacker=squad-friend");
    ASSERT_EQ(second_misses.size(), 2U);
    EXPECT_GE(second_misses[1].tick - second_misses[0].tick, cooldown);
}

TEST(WfsRuntimeCombatTest, InfantryEngagementSuppressesAndLosesContact) {
    SimState state = MakeState();
    for (std::uint64_t i = 0U; i < 600U; ++i) {
        step_sim_state(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "COMBAT_HIT attacker=squad-friend target=squad-enemy"))
        << "步兵接敌应产生命中事件";
    EXPECT_TRUE(HasEvent(state.event_log, "COMBAT_HIT attacker=squad-enemy target=squad-friend")) << "敌方应回击命中";
    RuntimeUnitState* enemy_unit = FindUnit(state, "squad-enemy");  // 弃车可能重分配，按 id 重查。
    ASSERT_NE(enemy_unit, nullptr);
    EXPECT_TRUE(enemy_unit->out_of_contact || HasEvent(state.event_log, "COMBAT_OUT_OF_CONTACT unit=squad-enemy"))
        << "压制达阈值后应触发失联（FR-065）";
    RuntimeUnitState* friend_unit = FindUnit(state, "squad-friend");
    ASSERT_NE(friend_unit, nullptr);
    EXPECT_GT(enemy_unit->suppression, 0.2) << "失联单位应保持明显压制";
    EXPECT_GT(friend_unit->suppression + enemy_unit->suppression, 0.2) << "接敌双方应积累压制";
}

TEST(WfsRuntimeCombatTest, VehiclePenetrationModuleDamageAbandonCreatesTwoDismountedSquads) {
    SimState state = MakeState();
    for (std::uint64_t i = 0U; i < 600U; ++i) {
        step_sim_state(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "COMBAT_DAMAGE target=vehicle-friend")) << "载具应被击穿并产生伤害";
    EXPECT_TRUE(HasEvent(state.event_log, "COMBAT_MODULE_DAMAGE unit=vehicle-friend") ||
                HasEvent(state.event_log, "COMBAT_VEHICLE_SEVERE unit=vehicle-friend") ||
                HasEvent(state.event_log, "COMBAT_VEHICLE_DESTROYED unit=vehicle-friend"))
        << "击穿后应推进模块损伤/严重受损/摧毁状态";
    EXPECT_TRUE(HasEvent(state.event_log, "COMBAT_ABANDONED unit=vehicle-friend"))
        << "摧毁或严重受损弃车应产生弃车事件";
    const auto abandoned_events = EventsWith(state.event_log, "COMBAT_ABANDONED unit=vehicle-friend");
    ASSERT_FALSE(abandoned_events.empty());
    EXPECT_NE(abandoned_events.front().message.find("crew=2 passengers=5"), std::string::npos)
        << "弃车日志应区分乘员组/载员组人数（FR-062）";

    RuntimeUnitState* vehicle = FindUnit(state, "vehicle-friend");
    ASSERT_NE(vehicle, nullptr);
    EXPECT_TRUE(vehicle->soldiers.empty()) << "弃车后载具不得保留乘员/载员空壳（F3）";
    RuntimeUnitState* crew_squad = FindUnit(state, "vehicle-friend-dismounted-crew");
    RuntimeUnitState* passenger_squad = FindUnit(state, "vehicle-friend-dismounted-passengers");
    ASSERT_NE(crew_squad, nullptr) << "乘员组徒步班组应存在（FR-062）";
    ASSERT_NE(passenger_squad, nullptr) << "载员组徒步班组应存在（FR-062）";
    EXPECT_EQ(crew_squad->crew_count, crew_squad->soldiers.size());
    EXPECT_EQ(passenger_squad->crew_count, passenger_squad->soldiers.size());
    EXPECT_GT(crew_squad->soldiers.size() + passenger_squad->soldiers.size(), 0U);
}

TEST(WfsRuntimeCombatTest, AmphibiousDerivedFromDataAndHeavyEquipment) {
    SimState state = MakeState();
    RuntimeUnitState* swim = FindUnit(state, "squad-swim");
    RuntimeUnitState* heavy = FindUnit(state, "squad-heavy");
    RuntimeUnitState* vehicle = FindUnit(state, "vehicle-friend");
    ASSERT_NE(swim, nullptr);
    ASSERT_NE(heavy, nullptr);
    ASSERT_NE(vehicle, nullptr);
    EXPECT_TRUE(swim->amphibious) << "无重装备步兵应可泅渡（FR-022）";
    EXPECT_FALSE(heavy->amphibious) << "携带重装备（HMG）步兵不可泅渡（FR-022）";
    for (const auto& soldier : heavy->soldiers) {
        EXPECT_TRUE(soldier.carries_heavy_equipment);
    }
    EXPECT_FALSE(vehicle->amphibious) << "载具两栖能力应来自数据（vehicle-ifv-test=false）";

    // 移动任务：泅渡单位越过河流，重装备单位在深水被阻塞。
    const auto swim_result = inject_player_command(state, MoveCommand("squad-swim", 0.32, 0.32), CommandSchema());
    ASSERT_TRUE(swim_result.accepted);
    const auto heavy_result = inject_player_command(state, MoveCommand("squad-heavy", 0.32, 0.32), CommandSchema());
    ASSERT_TRUE(heavy_result.accepted);
    for (std::uint64_t i = 0U; i < 400U; ++i) {
        step_sim_state(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "UNIT_MOVING unit=squad-swim"));
    EXPECT_TRUE(HasEvent(state.event_log, "UNIT_STUCK unit=squad-heavy")) << "非两栖单位进入深水应被阻塞（UNIT_STUCK）";
    // 载具弃车可能新增班组使旧指针失效：按 id 重新查找后再断言位置。
    RuntimeUnitState* heavy_after = FindUnit(state, "squad-heavy");
    RuntimeUnitState* swim_after = FindUnit(state, "squad-swim");
    ASSERT_NE(heavy_after, nullptr);
    ASSERT_NE(swim_after, nullptr);
    EXPECT_DOUBLE_EQ(heavy_after->x, 0.31);
    EXPECT_DOUBLE_EQ(heavy_after->y, 0.31);
    EXPECT_TRUE(swim_after->x > 0.300001 || swim_after->y > 0.300001) << "泅渡单位应越过深水前进";
}

TEST(WfsRuntimeCombatTest, InvalidCombatConfigRejectedExplicitly) {
    EXPECT_THROW(wfs::sim::CombatConfig::FromScenario(
                     nlohmann::json{{"combat", nlohmann::json{{"moving_target_factor", -1.0}}}}),
                 std::invalid_argument);
    EXPECT_THROW(wfs::sim::CombatConfig::FromScenario(nlohmann::json{
                     {"combat", nlohmann::json{{"contact_loss_max_ticks", 100U}, {"contact_loss_min_ticks", 200U}}}}),
                 std::invalid_argument);
    EXPECT_THROW(
        wfs::sim::CombatConfig::FromScenario(nlohmann::json{
            {"combat", nlohmann::json{{"suppression_recovery_per_tick", std::numeric_limits<double>::infinity()}}}}),
        std::invalid_argument);
}
