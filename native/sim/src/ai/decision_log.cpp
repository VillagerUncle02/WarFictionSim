// sim/src/ai/decision_log.cpp
//
// T020：AI 决策日志（决策点记录）实现。
//
// 实现策略：
// - 追加即保存：记录顺序 = 注入尝试顺序（含被拒绝的决策），与事件日志/队列
//   序列号一致，保证同一输入序列产生同一日志（宪法第 7 条）。
// - to_json/from_json 是存档与状态哈希共用的唯一序列化路径：字段名固定，
//   加载时缺失字段显式报错（宪法第 13/17 条）。

#include "wfs/sim/ai/decision_log.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/command_validation.h"

namespace wfs::sim {

void DecisionLog::append(AiDecisionRecord record) {
    entries_.push_back(std::move(record));
}

nlohmann::json decision_record_to_json(const AiDecisionRecord& record) {
    nlohmann::json errors = nlohmann::json::array();
    for (const ValidationError& error : record.validation_errors) {
        errors.push_back(nlohmann::json{{"code", error.code}, {"message", error.message}});
    }
    return nlohmann::json{
        {"decision_id", record.decision_id},
        {"node_id", record.node_id},
        {"trigger", record.trigger},
        {"arrival_tick", record.arrival_tick},
        {"arrival_seq", record.arrival_seq},
        {"state_hash", record.state_hash},
        {"input_json", record.input_json},
        {"events_json", record.events_json},
        {"output_json", record.output_json},
        {"validation_ok", record.validation_ok},
        {"validation_errors", std::move(errors)},
    };
}

nlohmann::json decision_log_to_json(const DecisionLog& log) {
    nlohmann::json entries = nlohmann::json::array();
    for (const AiDecisionRecord& record : log.entries()) {
        entries.push_back(decision_record_to_json(record));
    }
    return nlohmann::json{{"entries", std::move(entries)}};
}

DecisionLog decision_log_from_json(const nlohmann::json& json) {
    DecisionLog log;
    for (const nlohmann::json& entry : json.at("entries")) {
        AiDecisionRecord record;
        record.decision_id = entry.at("decision_id").get<std::string>();
        record.node_id = entry.at("node_id").get<std::string>();
        record.trigger = entry.at("trigger").get<std::string>();
        record.arrival_tick = entry.at("arrival_tick").get<GameTick>();
        record.arrival_seq = entry.at("arrival_seq").get<std::uint64_t>();
        record.state_hash = entry.at("state_hash").get<std::string>();
        record.input_json = entry.at("input_json").get<std::string>();
        record.events_json = entry.at("events_json").get<std::string>();
        record.output_json = entry.at("output_json").get<std::string>();
        record.validation_ok = entry.at("validation_ok").get<bool>();
        for (const nlohmann::json& error : entry.at("validation_errors")) {
            record.validation_errors.push_back(
                ValidationError{error.at("code").get<std::string>(), error.at("message").get<std::string>()});
        }
        log.append(std::move(record));
    }
    return log;
}

}  // namespace wfs::sim
