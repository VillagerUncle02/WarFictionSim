// sim/include/wfs/sim/headless.h
//
// T019：无头驱动公开接口（宪法 15：模拟核心无头可测）。
//
// 设计契约（quickstart §3；宪法 7/15/17）：
// - 无头 CLI（sim/headless/main.cpp）与黄金测试（T022）共用同一驱动实现，
//   保证"无头 CLI run --hash 与库内 SimState 哈希一致"（跨运行形态一致性）。
// - run_headless：加载场景 → （可选）脚本 AI 决策注入 → 推进固定 tick 数 →
//   返回最终状态哈希/事件/决策日志；线程数只影响性能不影响哈希。
// - inject_headless：逐行注入玩家命令脚本（每行一条命令 JSON），任一行为
//   校验拒绝则整体失败（宪法 17：显式报错，不静默吞错）。
// - save_headless：创建初始状态并写 WFS-SAVE 存档（T017）。
// - 所有结果携带 error 字段：空 = 成功；失败不产生部分输出。

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "wfs/sim/ai/decision_log.h"
#include "wfs/sim/ai/ia_backend.h"
#include "wfs/sim/event_log.h"

namespace wfs::sim {

struct HeadlessRunOptions {
    std::filesystem::path scenario_path;
    std::uint64_t seed = 0U;
    int threads = 4;  // 只影响性能，不影响状态哈希（宪法 7）。
    std::uint64_t ticks = 1200U;  // 默认运行时长（20 Hz 下 60 游戏秒）。
    std::string ai_backend = kAiBackendNone;  // kAiBackendScript / kAiBackendCloud / kAiBackendNone。
};

struct HeadlessRunResult {
    bool ok() const noexcept { return error.empty(); }

    std::string error;
    std::string state_hash;  // 最终状态 SHA-256（64 小写 hex，T016）。
    std::vector<SimEvent> events;            // 事件日志（seq 升序）。
    std::vector<AiDecisionRecord> decisions;  // 决策日志（注入顺序）。
};

struct HeadlessInjectOptions {
    std::filesystem::path scenario_path;
    std::uint64_t seed = 0U;
    int threads = 4;
    std::uint64_t ticks = 1200U;
    std::filesystem::path script_path;  // 每行一条玩家命令 JSON（# 开头为注释）。
};

struct HeadlessInjectResult {
    bool ok() const noexcept { return error.empty(); }

    std::string error;
    std::string state_hash;
    std::vector<SimEvent> events;
};

struct HeadlessSaveOptions {
    std::filesystem::path scenario_path;
    std::uint64_t seed = 0U;
    int threads = 4;
    std::filesystem::path out_path;  // WFS-SAVE 存档输出路径。
};

struct HeadlessSaveResult {
    bool ok() const noexcept { return error.empty(); }

    std::string error;
};

// 无头运行：脚本 AI（T021）或 none；cloud 未实现时显式报错（T080 后置）。
HeadlessRunResult run_headless(const HeadlessRunOptions& options);

// 无头命令脚本注入 + 推进（quickstart §3.2 基础形态）。
HeadlessInjectResult inject_headless(const HeadlessInjectOptions& options);

// 无头存档写入（初始状态，T017 WFS-SAVE 格式）。
HeadlessSaveResult save_headless(const HeadlessSaveOptions& options);

}  // namespace wfs::sim
