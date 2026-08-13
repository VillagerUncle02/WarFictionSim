// tests/sim_tests/command_validation_test.cpp
//
// T014 单元测试：命令 Schema + 语义双重校验管道。
// 覆盖合法命令通过、未注册类型拒绝、目标不存在/越权拒绝、完成条件不可求值
// （未知条件/缺参数/未知区域/未知目标单位）、弹药覆盖不存在拒绝、
// Schema 结构错误、类型注册表注入，以及错误顺序与内容的确定性。

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/command_validation.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/model/mission.h"

namespace {

using wfs::sim::CommandValidationContext;
using wfs::sim::CommandValidationResult;
using wfs::sim::load_scenario;
using wfs::sim::make_validation_context;
using wfs::sim::UnitInfo;
using wfs::sim::validate_command;
using wfs::sim::ValidationError;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path CommandSchema() {
    return RepoRoot() / "contracts" / "schemas" / "command.schema.json";
}

const nlohmann::json& CommandSchemaJson() {
    // 解析后的 Schema 只读复用：避免每次校验都重复读文件，也让 json 重载
    // 以精确类型调用（消除 string/path 重载间的隐式转换歧义）。
    static const nlohmann::json schema = [] {
        std::ifstream in(CommandSchema());
        return nlohmann::json::parse(in);
    }();
    return schema;
}

CommandValidationContext DefaultContext() {
    CommandValidationContext context;
    context.commander_node_id = "platoon-alpha";
    context.units = {
        UnitInfo{"squad-a", "platoon-alpha", {"5.56mm", "frag_grenade"}},
        UnitInfo{"squad-b", "platoon-alpha", {"5.56mm"}},
        UnitInfo{"squad-c", "enemy-command", {"7.62mm"}},
    };
    context.known_zones = {"zone-hill"};
    return context;
}

nlohmann::json ValidCommandJson() {
    return nlohmann::json{
        {"schema_version", 1},
        {"type", "SECURE_ZONE"},
        {"target", nlohmann::json{{"kind", "unit"}, {"ref", "squad-a"}}},
        {"completion", nlohmann::json{{"condition", "secure_zone"},
                                      {"params", nlohmann::json{{"zone", "zone-hill"}, {"duration_ticks", 1200}}}}},
        {"intent", "占领高地并坚守"},
        {"behavior",
         nlohmann::json{{"engagement", "aggressive"}, {"ammo_override", "5.56mm"}, {"failure_action", "hold"}}},
        {"priority", 1},
        {"deadline", nlohmann::json{{"game_time", 3600}}},
    };
}

std::vector<std::string> ErrorCodes(const CommandValidationResult& result) {
    std::vector<std::string> codes;
    codes.reserve(result.errors.size());
    for (const ValidationError& error : result.errors) {
        codes.push_back(error.code);
    }
    return codes;
}

CommandValidationResult Validate(const nlohmann::json& command) {
    return validate_command(command, DefaultContext(), CommandSchemaJson());
}

}  // namespace

TEST(WfsCommandValidationTest, ValidCommandPasses) {
    const CommandValidationResult result = Validate(ValidCommandJson());
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.errors.empty());
}

TEST(WfsCommandValidationTest, SchemaRejectsMissingRequiredField) {
    nlohmann::json command = ValidCommandJson();
    command.erase("priority");
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "SCHEMA_INVALID");
}

TEST(WfsCommandValidationTest, InvalidJsonRejected) {
    const CommandValidationResult result = validate_command("{ not json", DefaultContext(), CommandSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "INVALID_JSON");
}

TEST(WfsCommandValidationTest, UnregisteredTypeRejected) {
    nlohmann::json command = ValidCommandJson();
    command["type"] = "TELEPORT";
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "UNREGISTERED_TYPE");
}

TEST(WfsCommandValidationTest, DefaultTypeTableMatchesMissionRegistry) {
    // F5：默认内置类型表由 model::registered_mission_types() 生成，
    // 13 种任务类型必须全部可校验通过（消除双源漂移）。
    for (const wfs::sim::model::MissionType type : wfs::sim::model::registered_mission_types()) {
        nlohmann::json command = ValidCommandJson();
        command["type"] = wfs::sim::model::to_string(type);
        if (type == wfs::sim::model::MissionType::kSupportRequest) {
            // T047：SUPPORT_REQUEST 有专用完成条件与 support 负载契约
            // （command-schema §1.1），按契约形状构造后仍须通过校验。
            command["completion"]["condition"] = "support";
            command["support"] =
                nlohmann::json{{"request_type", "reinforce"}, {"kinds", nlohmann::json::array({"squad-mortar-team"})}};
        }
        const CommandValidationResult result = Validate(command);
        EXPECT_TRUE(result.ok()) << wfs::sim::model::to_string(type);
    }
}

TEST(WfsCommandValidationTest, SupportRequestTypeEnumRejectsUnknownAndAcceptsAllContractValues) {
    // US2 R2：support.request_type 契约枚举（FR-050）：非法枚举在 Schema
    // 第一层拒绝（SCHEMA_INVALID，不抛异常），5 种合法值全部通过。
    const auto support_command = [](const char* request_type) {
        nlohmann::json command = ValidCommandJson();
        command["type"] = "SUPPORT_REQUEST";
        command["completion"] = nlohmann::json{{"condition", "support"}, {"params", nlohmann::json::object()}};
        command["support"] = nlohmann::json{
            {"request_type", request_type},
            {"kinds", nlohmann::json::array({"squad-mortar-team"})},
            {"quantity", 1},
            {"to_node", "node-battalion-1"},
        };
        return command;
    };

    // 负例：request_type="teleport" 不在 5 种契约枚举内，第一层 Schema 拒绝。
    const CommandValidationResult invalid = Validate(support_command("teleport"));
    EXPECT_FALSE(invalid.ok());
    ASSERT_FALSE(invalid.errors.empty());
    EXPECT_EQ(invalid.errors.front().code, "SCHEMA_INVALID");

    // 正例：reinforce/fire_support/engineer/medical/logistics 各可通过
    // 第一层（整体校验通过，含语义层）。
    for (const char* request_type : {"reinforce", "fire_support", "engineer", "medical", "logistics"}) {
        const CommandValidationResult valid = Validate(support_command(request_type));
        EXPECT_TRUE(valid.ok()) << request_type;
    }
}

TEST(WfsCommandValidationTest, TargetNotFoundRejected) {
    nlohmann::json command = ValidCommandJson();
    command["target"] = nlohmann::json{{"kind", "unit"}, {"ref", "ghost-unit"}};
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "TARGET_NOT_FOUND");
}

TEST(WfsCommandValidationTest, UnauthorizedTargetRejected) {
    nlohmann::json command = ValidCommandJson();
    command["target"] = nlohmann::json{{"kind", "unit"}, {"ref", "squad-c"}};
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "UNAUTHORIZED_TARGET");
}

TEST(WfsCommandValidationTest, EmptyCommanderSkipsAuthorityCheck) {
    nlohmann::json command = ValidCommandJson();
    command["target"] = nlohmann::json{{"kind", "unit"}, {"ref", "squad-c"}};
    command["behavior"].erase("ammo_override");  // 越权检查与弹药检查独立。
    CommandValidationContext context = DefaultContext();
    context.commander_node_id.clear();
    const CommandValidationResult result = validate_command(command, context, CommandSchemaJson());
    EXPECT_TRUE(result.ok());
}

TEST(WfsCommandValidationTest, UnknownConditionRejected) {
    nlohmann::json command = ValidCommandJson();
    command["completion"] = nlohmann::json{{"condition", "teleport"}, {"params", nlohmann::json::object()}};
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "CONDITION_NOT_EVALUABLE");
}

TEST(WfsCommandValidationTest, MissingConditionParamRejected) {
    nlohmann::json command = ValidCommandJson();
    command["completion"] =
        nlohmann::json{{"condition", "secure_zone"}, {"params", nlohmann::json{{"duration_ticks", 1200}}}};
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "CONDITION_NOT_EVALUABLE");
}

TEST(WfsCommandValidationTest, UnknownConditionZoneRejected) {
    nlohmann::json command = ValidCommandJson();
    command["completion"] = nlohmann::json{
        {"condition", "secure_zone"}, {"params", nlohmann::json{{"zone", "zone-ghost"}, {"duration_ticks", 1200}}}};
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "CONDITION_NOT_EVALUABLE");
}

TEST(WfsCommandValidationTest, UnknownConditionTargetUnitRejected) {
    nlohmann::json command = ValidCommandJson();
    command["completion"] =
        nlohmann::json{{"condition", "destroy_unit"}, {"params", nlohmann::json{{"target_unit", "ghost-unit"}}}};
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "CONDITION_NOT_EVALUABLE");
}

TEST(WfsCommandValidationTest, AmmoOverrideMissingRejected) {
    nlohmann::json command = ValidCommandJson();
    command["behavior"]["ammo_override"] = "rpg-7";
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "AMMO_NOT_FOUND");
}

TEST(WfsCommandValidationTest, AmmoOverrideOnZoneTargetRejectedAsNotEvaluable) {
    nlohmann::json command = ValidCommandJson();
    command["target"] = nlohmann::json{{"kind", "zone"}, {"ref", "zone-hill"}};
    command["behavior"]["ammo_override"] = "smoke";
    const CommandValidationResult result = Validate(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors.front().code, "AMMO_NOT_EVALUABLE");
}

TEST(WfsCommandValidationTest, RegisteredTypesOverrideBuiltInList) {
    CommandValidationContext context = DefaultContext();
    context.registered_types = {"CUSTOM_MISSION"};

    nlohmann::json command = ValidCommandJson();
    const CommandValidationResult built_in_rejected = validate_command(command, context, CommandSchemaJson());
    ASSERT_FALSE(built_in_rejected.ok());
    EXPECT_EQ(built_in_rejected.errors.front().code, "UNREGISTERED_TYPE");

    command["type"] = "CUSTOM_MISSION";
    const CommandValidationResult custom_passes = validate_command(command, context, CommandSchemaJson());
    EXPECT_TRUE(custom_passes.ok());
}

TEST(WfsCommandValidationTest, ErrorsAreDeterministicAndOrdered) {
    nlohmann::json command = ValidCommandJson();
    command["type"] = "TELEPORT";
    command["completion"] = nlohmann::json{{"condition", "teleport"}, {"params", nlohmann::json::object()}};
    command["behavior"]["ammo_override"] = "rpg-7";

    const CommandValidationResult first = Validate(command);
    const CommandValidationResult second = Validate(command);
    ASSERT_FALSE(first.ok());
    ASSERT_EQ(first.errors.size(), second.errors.size());
    for (std::size_t i = 0; i < first.errors.size(); ++i) {
        EXPECT_EQ(first.errors[i].code, second.errors[i].code);
        EXPECT_EQ(first.errors[i].message, second.errors[i].message);
    }
    EXPECT_EQ(ErrorCodes(first),
              (std::vector<std::string>{"UNREGISTERED_TYPE", "CONDITION_NOT_EVALUABLE", "AMMO_NOT_FOUND"}));
}

TEST(WfsCommandValidationTest, ScenarioContextMapping) {
    const auto load = load_scenario(RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json");
    ASSERT_TRUE(load.ok());
    const CommandValidationContext context = make_validation_context(load.scenario);
    EXPECT_EQ(context.commander_node_id, "platoon-alpha");
    ASSERT_EQ(context.units.size(), 3u);
    EXPECT_EQ(context.units[2].id, "squad-c");
    EXPECT_EQ(context.known_zones, (std::vector<std::string>{"zone-hill"}));
}
