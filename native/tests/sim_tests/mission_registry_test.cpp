// tests/sim_tests/mission_registry_test.cpp
//
// T028 单元测试：任务模型与 13 种任务类型注册表。
// 覆盖：MissionType 全量注册（data-model §11/FR-042）、持续任务与侦察类任务
// 标识、任务状态机数据表（下达/执行/完成/失败/超时/取消，FR-044）、
// Mission 序列化往返、ConditionExpr 确定性求值（同输入同结果、短路、缺失
// 变量显式报错）与 JSON 往返。

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/model/mission.h"

namespace {

namespace model = wfs::sim::model;

using model::AmmoPolicy;
using model::CautionLevel;
using model::ConditionExpr;
using model::EngagementPolicy;
using model::FailureAction;
using model::Mission;
using model::MissionState;
using model::MissionTarget;
using model::MissionType;

nlohmann::json RoundTrip(const nlohmann::json& input) {
    return nlohmann::json::parse(input.dump());
}

const std::vector<std::string> kExpectedTypeNames = {
    "MOVE",    "PATROL",       "ATTACK",           "DEFEND",           "SECURE_ZONE", "CLEAR",           "DRIVE_OUT",
    "FORTIFY", "HIDDEN_RECON", "INFILTRATE_RECON", "OBSERVATION_POST", "FIRE_RECON",  "SUPPORT_REQUEST",
};

}  // namespace

TEST(WfsMissionRegistryTest, AllThirteenTypesRegistered) {
    const std::vector<MissionType>& types = model::registered_mission_types();
    ASSERT_EQ(types.size(), 13u);
    ASSERT_EQ(kExpectedTypeNames.size(), 13u);
    for (std::size_t i = 0; i < types.size(); ++i) {
        EXPECT_EQ(model::to_string(types[i]), kExpectedTypeNames[i]);
        EXPECT_TRUE(model::is_registered_mission_type(kExpectedTypeNames[i]));
        EXPECT_EQ(model::mission_type_from_string(kExpectedTypeNames[i]), types[i]);
    }
    EXPECT_FALSE(model::is_registered_mission_type("FIRE_GUIDANCE"));
    EXPECT_THROW(model::mission_type_from_string("FIRE_GUIDANCE"), std::invalid_argument);
}

TEST(WfsMissionRegistryTest, ContinuousAndReconFlags) {
    EXPECT_TRUE(model::is_continuous_mission(MissionType::kPatrol));
    EXPECT_TRUE(model::is_continuous_mission(MissionType::kObservationPost));
    EXPECT_FALSE(model::is_continuous_mission(MissionType::kAttack));
    EXPECT_FALSE(model::is_continuous_mission(MissionType::kMove));

    EXPECT_TRUE(model::is_recon_mission(MissionType::kHiddenRecon));
    EXPECT_TRUE(model::is_recon_mission(MissionType::kInfiltrateRecon));
    EXPECT_TRUE(model::is_recon_mission(MissionType::kObservationPost));
    EXPECT_TRUE(model::is_recon_mission(MissionType::kFireRecon));
    EXPECT_FALSE(model::is_recon_mission(MissionType::kSecureZone));
}

TEST(WfsMissionRegistryTest, StateMachineTransitions) {
    EXPECT_TRUE(model::can_transition(MissionState::kIssued, MissionState::kExecuting));
    EXPECT_TRUE(model::can_transition(MissionState::kIssued, MissionState::kCancelled));
    EXPECT_TRUE(model::can_transition(MissionState::kExecuting, MissionState::kCompleted));
    EXPECT_TRUE(model::can_transition(MissionState::kExecuting, MissionState::kFailed));
    EXPECT_TRUE(model::can_transition(MissionState::kExecuting, MissionState::kTimedOut));
    EXPECT_TRUE(model::can_transition(MissionState::kExecuting, MissionState::kCancelled));
    // FR-044：超时后由指挥官决定继续、取消或判失败。
    EXPECT_TRUE(model::can_transition(MissionState::kTimedOut, MissionState::kExecuting));
    EXPECT_TRUE(model::can_transition(MissionState::kTimedOut, MissionState::kCancelled));
    EXPECT_TRUE(model::can_transition(MissionState::kTimedOut, MissionState::kFailed));

    EXPECT_FALSE(model::can_transition(MissionState::kCompleted, MissionState::kExecuting));
    EXPECT_FALSE(model::can_transition(MissionState::kFailed, MissionState::kCompleted));
    EXPECT_FALSE(model::can_transition(MissionState::kCancelled, MissionState::kExecuting));
    EXPECT_TRUE(model::is_terminal_mission_state(MissionState::kCompleted));
    EXPECT_TRUE(model::is_terminal_mission_state(MissionState::kFailed));
    EXPECT_TRUE(model::is_terminal_mission_state(MissionState::kCancelled));
    EXPECT_FALSE(model::is_terminal_mission_state(MissionState::kExecuting));
}

TEST(WfsMissionRegistryTest, MissionRoundTrip) {
    Mission mission;
    mission.id = "mission-1";
    mission.type = MissionType::kSecureZone;
    mission.target = MissionTarget{"zone", "zone-hill"};
    mission.completion_condition = "secure_zone";
    mission.completion_params = nlohmann::json{{"zone", "zone-hill"}, {"duration_ticks", 1200}};
    mission.intent = "占领高地并坚守";
    mission.behavior.engagement = EngagementPolicy::kAggressive;
    mission.behavior.caution = CautionLevel::kConservative;
    mission.behavior.ammo_policy = AmmoPolicy::kSpecific;
    mission.behavior.ammo_override = "ammo-556";
    mission.behavior.failure_action = FailureAction::kWithdrawTo;
    mission.behavior.failure_target = "zone-start";
    mission.priority = 1;
    mission.deadline_ticks = 3600U;
    mission.state = MissionState::kExecuting;
    mission.continuous = false;
    mission.loops = false;

    const Mission restored = RoundTrip(nlohmann::json(mission)).get<Mission>();
    EXPECT_EQ(restored, mission);
    EXPECT_EQ(restored.completion_params.at("duration_ticks").get<std::uint64_t>(), 1200u);
}

TEST(WfsMissionRegistryTest, MissionStateStrings) {
    EXPECT_EQ(model::to_string(MissionState::kIssued), "ISSUED");
    EXPECT_EQ(model::to_string(MissionState::kTimedOut), "TIMED_OUT");
    EXPECT_EQ(model::mission_state_from_string("EXECUTING"), MissionState::kExecuting);
    EXPECT_THROW(model::mission_state_from_string("PAUSED"), std::invalid_argument);
}

TEST(WfsConditionExprTest, ParsesAndEvaluatesDeterministically) {
    const nlohmann::json json = nlohmann::json{
        {"op", "and"},
        {"children", nlohmann::json::array({{{"op", "gt"}, {"variable", "enemies_in_zone"}, {"threshold", 0}},
                                            {{"op", "le"}, {"variable", "own_strength"}, {"threshold", 0.5}}})}};
    const ConditionExpr expr = ConditionExpr::Parse(json);
    const std::map<std::string, double> satisfied{{"enemies_in_zone", 3.0}, {"own_strength", 0.4}};
    EXPECT_TRUE(expr.Evaluate(satisfied));
    EXPECT_TRUE(expr.Evaluate(satisfied));  // 同输入同结果（宪法第 7 条）

    const std::map<std::string, double> not_satisfied{{"enemies_in_zone", 0.0}, {"own_strength", 0.9}};
    EXPECT_FALSE(expr.Evaluate(not_satisfied));
    EXPECT_FALSE(expr.Evaluate(not_satisfied));
}

TEST(WfsConditionExprTest, ComparisonOperatorsAndNot) {
    const ConditionExpr eq = ConditionExpr::Compare(model::ConditionOp::kEq, "a", 1.0);
    EXPECT_TRUE(eq.Evaluate({{"a", 1.0}}));
    EXPECT_FALSE(eq.Evaluate({{"a", 2.0}}));

    const ConditionExpr ne = ConditionExpr::Compare(model::ConditionOp::kNe, "a", 1.0);
    EXPECT_TRUE(ne.Evaluate({{"a", 2.0}}));

    const ConditionExpr not_gt =
        ConditionExpr::Combine(model::ConditionOp::kNot, {ConditionExpr::Compare(model::ConditionOp::kGt, "a", 5.0)});
    EXPECT_TRUE(not_gt.Evaluate({{"a", 3.0}}));
    EXPECT_FALSE(not_gt.Evaluate({{"a", 7.0}}));
}

TEST(WfsConditionExprTest, ShortCircuitIsDeterministic) {
    const ConditionExpr expr = ConditionExpr::Combine(model::ConditionOp::kAnd,
                                                      {ConditionExpr::Compare(model::ConditionOp::kEq, "present", 1.0),
                                                       ConditionExpr::Compare(model::ConditionOp::kEq, "absent", 1.0)});
    // 第一个条件为假时短路，不访问缺失变量（结果与上下文无关、确定性）。
    EXPECT_FALSE(expr.Evaluate({{"present", 0.0}}));
    EXPECT_FALSE(expr.Evaluate({{"present", 0.0}}));
}

TEST(WfsConditionExprTest, MissingVariableReportsError) {
    const ConditionExpr expr = ConditionExpr::Compare(model::ConditionOp::kGt, "ghost", 0.0);
    EXPECT_THROW(expr.Evaluate({{"other", 1.0}}), std::invalid_argument);
}

TEST(WfsConditionExprTest, JsonRoundTripAndInvalidInput) {
    const nlohmann::json json = nlohmann::json{
        {"op", "or"},
        {"children", nlohmann::json::array({{{"op", "true"}}, {{"op", "lt"}, {"variable", "x"}, {"threshold", 2.5}}})}};
    const ConditionExpr expr = ConditionExpr::Parse(json);
    const ConditionExpr restored = RoundTrip(expr.ToJson()).get<ConditionExpr>();
    EXPECT_EQ(restored, expr);

    EXPECT_THROW(ConditionExpr::Parse(nlohmann::json{{"op", "xor"}}), std::invalid_argument);
    EXPECT_THROW(ConditionExpr::Parse(nlohmann::json{{"op", "not"}}), std::invalid_argument);
    EXPECT_THROW(ConditionExpr::Parse(nlohmann::json{{"op", "gt"}}), std::invalid_argument);
}
