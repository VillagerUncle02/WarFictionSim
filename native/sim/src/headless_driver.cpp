// sim/src/headless_driver.cpp
//
// T019：无头驱动实现。
//
// 实现策略（宪法 15/7/17；quickstart §3）：
// - 与 C ABI 共用同一内部推进/注入路径（sim_runtime.cpp / ai_inject.cpp），
//   因此无头 CLI 与库内 SimState 必然同构（T022 跨运行形态一致性）。
// - run 的脚本 AI 路径：tick 0 为每个脚本决策节点生成一次决策并注入（到达
//   tick + 队列序列号进决策日志，CHK052 回放依据），随后推进固定 tick 数；
//   决策输入摘要显式排除线程数，1 与 N 线程哈希一致（宪法 7）。
// - 所有失败返回显式 error（宪法 17），不产生部分输出文件。

#include "wfs/sim/headless.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ai_inject.h"
#include "save.h"
#include "sim_runtime.h"
#include "sim_state.h"
#include "state_serialization.h"
#include "wfs/sim/ai/script_backend.h"
#include "wfs/sim/c_api.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/rng.h"

namespace wfs::sim {

namespace {

// 加载初始状态的运行配置（seed/threads 与场景路径解耦，避免相邻参数误判）。
struct LoadConfig {
    std::uint64_t seed = 0U;
    int threads = 1;
};

// 加载场景并构造初始 SimState；失败时 error 非空并返回 nullopt（SimState
// 因 EventLog explicit 构造器不可默认构造，错误路径不构造状态对象）。
std::optional<SimState> LoadState(const std::filesystem::path& scenario_path, const LoadConfig& config,
                                  std::string& error) {
    const ScenarioLoadResult load = load_scenario(scenario_path);
    if (!load.ok()) {
        const std::string message = load.issues.empty() ? "" : load.issues.front().message;
        error = "场景加载失败: " + message;
        return std::nullopt;
    }
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = Rng(config.seed, 0U);
    state.seed = config.seed;
    state.threads = config.threads;
    state.scenario_path = scenario_path;
    return state;
}

std::filesystem::path CommandSchemaPath(const std::filesystem::path& scenario_path) {
    return resolve_schema_path(scenario_path, "command.schema.json");
}

std::string FirstError(const std::vector<ValidationError>& errors) {
    return errors.empty() ? "UNKNOWN" : errors.front().code + ": " + errors.front().message;
}

// 推进固定 tick 数（与 C ABI wfs_sim_step 同一实现）。
void StepTicks(SimState& state, std::uint64_t ticks) {
    for (std::uint64_t i = 0U; i < ticks; ++i) {
        step_sim_state(state);
    }
}

bool IsKnownBackend(const std::string& backend) {
    return backend == kAiBackendScript || backend == kAiBackendCloud || backend == kAiBackendNone;
}

}  // namespace

HeadlessRunResult run_headless(const HeadlessRunOptions& options) {
    HeadlessRunResult result;
    if (options.threads < 1) {
        result.error = "threads 必须 >= 1";
        return result;
    }
    if (!IsKnownBackend(options.ai_backend)) {
        result.error = "未知 --ai-backend: " + options.ai_backend + "（可选 script/cloud/none）";
        return result;
    }

    std::string load_error;
    std::optional<SimState> loaded =
        LoadState(options.scenario_path, LoadConfig{options.seed, options.threads}, load_error);
    if (!loaded.has_value()) {
        result.error = std::move(load_error);
        return result;
    }
    SimState state = std::move(*loaded);

    const std::filesystem::path schema_path = CommandSchemaPath(options.scenario_path);
    if (schema_path.empty()) {
        result.error = "无法解析命令 Schema 路径";
        return result;
    }

    if (options.ai_backend == kAiBackendScript) {
        std::unique_ptr<IAiBackend> backend = create_script_backend();
        const std::vector<std::string> nodes = script_decision_nodes(state.scenario);
        for (const std::string& node : nodes) {
            // 决策策略：当前仅在 run_start 时每节点触发一次（无事件驱动、无
            // 频率上限）；T080 将接入关键事件触发（FR-036）与每节点频率上限
            // （FR-052），并在此填充 last_decision_tick / rate_limit_interval_ticks。
            const AiDecisionInput input{node, "run_start", state.clock.tick(), build_ai_input_summary(state),
                                        build_ai_events_json(state)};
            const AiDecision decision = backend->decide(input);
            if (!decision.ok()) {
                result.error = "脚本 AI 决策失败（node=" + node + "）: " + decision.error;
                return result;
            }
            const AiDecisionMeta meta{"script-" + node + "-" + std::to_string(state.clock.tick()), node, "run_start"};
            const AiInjectResult injected = inject_ai_decision(state, decision.command_json, meta, schema_path);
            if (!injected.accepted) {
                result.error = "脚本 AI 决策被校验拒绝（node=" + node + "）: " + FirstError(injected.errors);
                return result;
            }
        }
    } else if (options.ai_backend == kAiBackendCloud) {
        result.error = "云端 AI 后端尚未实现（T080 后置）；请使用 --ai-backend script 或 none";
        return result;
    }

    StepTicks(state, options.ticks);
    result.state_hash = compute_state_hash_hex(state);
    result.events = state.event_log.events();
    result.decisions = state.decision_log.entries();
    return result;
}

HeadlessInjectResult inject_headless(const HeadlessInjectOptions& options) {
    HeadlessInjectResult result;
    if (options.threads < 1) {
        result.error = "threads 必须 >= 1";
        return result;
    }

    std::string load_error;
    std::optional<SimState> loaded =
        LoadState(options.scenario_path, LoadConfig{options.seed, options.threads}, load_error);
    if (!loaded.has_value()) {
        result.error = std::move(load_error);
        return result;
    }
    SimState state = std::move(*loaded);

    const std::filesystem::path schema_path = CommandSchemaPath(options.scenario_path);
    if (schema_path.empty()) {
        result.error = "无法解析命令 Schema 路径";
        return result;
    }

    std::ifstream script(options.script_path, std::ios::binary);
    if (!script) {
        result.error = "无法读取脚本文件: " + options.script_path.string();
        return result;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(script, line)) {
        ++line_number;
        const std::size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;  // 空行。
        }
        const std::size_t last = line.find_last_not_of(" \t\r\n");
        const std::string command = line.substr(first, last - first + 1U);
        if (command.starts_with("#")) {
            continue;  // 注释行。
        }
        const PlayerCommandResult injected = inject_player_command(state, command, schema_path);
        if (!injected.accepted) {
            result.error = "脚本第 " + std::to_string(line_number) + " 行命令被拒绝: " + FirstError(injected.errors);
            return result;
        }
    }

    StepTicks(state, options.ticks);
    result.state_hash = compute_state_hash_hex(state);
    result.events = state.event_log.events();
    return result;
}

HeadlessSaveResult save_headless(const HeadlessSaveOptions& options) {
    HeadlessSaveResult result;
    if (options.threads < 1) {
        result.error = "threads 必须 >= 1";
        return result;
    }
    if (options.out_path.empty()) {
        result.error = "save 需要 --out 存档路径";
        return result;
    }

    std::string load_error;
    std::optional<SimState> loaded =
        LoadState(options.scenario_path, LoadConfig{options.seed, options.threads}, load_error);
    if (!loaded.has_value()) {
        result.error = std::move(load_error);
        return result;
    }
    const wfs_sim_result save_result = save_to_file(*loaded, options.out_path);
    if (save_result != WFS_SIM_RESULT_OK) {
        result.error = "存档写入失败（错误码 " + std::to_string(static_cast<int>(save_result)) + "）";
    }
    return result;
}

}  // namespace wfs::sim
