// sim/src/ai/script_backend.cpp
//
// T021：无 AI 脚本后端实现。
//
// 实现策略（宪法 8/9；FR-068/069/SC-007）：
// - 规则选择：为节点内第一个单位（单位数组稳定顺序）下达 SECURE_ZONE 命令，
//   目标取场景第一个区域；输出与云端 LLM 完全同构的命令 JSON（FR-045）。
// - 确定性：decide() 是输入的纯函数——只解析 state_summary_json（注入通道
//   构建，不含线程数），不消费模拟 RNG、不读现实时钟，同一输入必然同一输出。
// - 兜底可诊断：无单位/无区域/非法输入返回显式 error（宪法 17 禁止静默吞错），
//   由调用方记录并跳过注入；脚本 AI 不参与任何数值结算与任务完成判定（宪法 9）。

#include "wfs/sim/ai/script_backend.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/ai/ia_backend.h"
#include "wfs/sim/loader.h"

namespace wfs::sim {

namespace {

class ScriptBackend final : public IAiBackend {
   public:
    std::string name() const override { return kAiBackendScript; }

    AiDecision decide(const AiDecisionInput& input) override {
        nlohmann::json summary;
        try {
            summary = nlohmann::json::parse(input.state_summary_json);
        } catch (const nlohmann::json::exception&) {
            return AiDecision{{}, "脚本 AI：决策输入不是合法 JSON"};
        }

        const nlohmann::json& zones = summary.value("zones", nlohmann::json::array());
        if (zones.empty()) {
            return AiDecision{{}, "脚本 AI：场景未定义区域，无法生成命令"};
        }
        const std::string zone = zones.front().get<std::string>();

        const nlohmann::json& units = summary.value("units", nlohmann::json::array());
        std::string unit_id;
        for (const nlohmann::json& unit : units) {
            if (unit.value("node_id", std::string{}) == input.node_id) {
                unit_id = unit.at("id").get<std::string>();
                break;
            }
        }
        if (unit_id.empty()) {
            return AiDecision{{}, "脚本 AI：节点 " + input.node_id + " 无可用单位"};
        }

        const nlohmann::json command{
            {"schema_version", 1},
            {"type", "SECURE_ZONE"},
            {"target", nlohmann::json{{"kind", "unit"}, {"ref", unit_id}}},
            {"completion", nlohmann::json{{"condition", "secure_zone"},
                                          {"params", nlohmann::json{{"zone", zone}, {"duration_ticks", 1200}}}}},
            {"intent", "脚本 AI 兜底：控制区域 " + zone},
            {"behavior", nlohmann::json{{"engagement", "balanced"}}},
            {"priority", 1},
            {"deadline", nlohmann::json{{"game_time", 3600}}},
        };
        return AiDecision{command.dump(), {}};
    }
};

}  // namespace

std::unique_ptr<IAiBackend> create_script_backend() {
    return std::make_unique<ScriptBackend>();
}

std::vector<std::string> script_decision_nodes(const Scenario& scenario) {
    std::vector<std::string> nodes;
    for (const ScenarioUnit& unit : scenario.units) {
        if (unit.node_id == scenario.player_node_id) {
            continue;  // 玩家节点由玩家指挥，脚本不接管。
        }
        if (std::find(nodes.begin(), nodes.end(), unit.node_id) == nodes.end()) {
            nodes.push_back(unit.node_id);
        }
    }
    return nodes;
}

}  // namespace wfs::sim
