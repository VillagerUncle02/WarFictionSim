// sim/headless/main.cpp
//
// T019：无头 CLI（sim_headless）入口。
//
// 实现策略（quickstart §3；宪法 15/17）：
// - 子命令 run/inject/save 全部委托给 headless_driver（wfs/sim/headless.h），
//   本文件只做参数解析、JSONL 事件写出与错误码映射（0 成功 / 1 运行错误 /
//   2 用法错误），保证 CLI 与库内 SimState 行为同构（T022 黄金测试）。
// - --hash 输出最终状态 SHA-256（64 小写 hex，唯一 stdout 内容，供 CI/测试
//   直接比对）；事件 JSONL 由 --out 指定，按事件日志 seq 升序输出。
// - 未知参数/缺参显式报错并返回 2；运行失败返回 1（宪法 17：不静默吞错）。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/headless.h"

namespace {

// 命令行整数解析基数（十进制；具名常量避免 magic number）。
constexpr int kParseBase = 10;

struct CliOptions {
    std::string subcommand;
    std::filesystem::path scenario;
    std::optional<std::uint64_t> seed;
    std::optional<int> threads;
    std::filesystem::path out;
    std::filesystem::path script;
    std::string ai_backend = wfs::sim::kAiBackendNone;
    std::optional<std::uint64_t> ticks;
    bool hash = false;
    bool has_hash = false;        // --hash 显式出现（save 子命令拒绝）。
    bool has_ticks = false;       // --ticks 显式出现（save 子命令拒绝）。
    bool has_script = false;      // --script 显式出现（run/save 子命令拒绝）。
    bool has_ai_backend = false;  // --ai-backend 显式出现（inject/save 子命令拒绝）。
    bool help = false;
};

std::string Usage() {
    return R"(用法:
  sim_headless <run|inject|save> [选项]

run:
  --scenario <path>    场景 JSON（必需）
  --seed <u64>         随机种子（默认 0）
  --threads <int>      并行度（默认 4；只影响性能不影响状态哈希）
  --ticks <u64>        运行 tick 数（默认 1200）
  --ai-backend <name>  script|cloud|none（默认 none；cloud 未实现时报错）
  --out <path>         事件 JSONL 输出文件（可选）
  --hash               打印最终状态 SHA-256 到 stdout

inject:
  --scenario/--seed/--threads/--ticks 同上
  --script <path>      命令脚本（每行一条命令 JSON，# 开头为注释，必需）
  --out <path>         事件 JSONL 输出文件（可选）
  --hash               打印最终状态 SHA-256（可选）

save:
  --scenario/--seed/--threads 同上
  --out <path>         存档输出路径（必需，WFS-SAVE 格式）

公共:
  --help               显示本帮助
)";
}

bool ParseUint64(const std::string& text, std::uint64_t& value) {
    // 拒绝空串与 '-' 开头：stoull 会把 "-1" 回绕成 UINT64_MAX（--ticks -1
    // 会让驱动死循环），全数字预检从根上消除该路径。
    if (text.empty() || text[0] == '-') {
        return false;
    }
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return false;
        }
    }
    try {
        std::size_t consumed = 0U;
        value = std::stoull(text, &consumed, kParseBase);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& text, int& value) {
    try {
        std::size_t consumed = 0U;
        value = std::stoi(text, &consumed, kParseBase);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

// 取下一个参数值；缺失时设置 error 并返回 nullptr。
const char* NextValue(int argc, char** argv, int& index, const std::string& key, std::string& error) {
    if (index + 1 >= argc) {
        error = "缺少参数值: " + key;
        return nullptr;
    }
    ++index;
    return argv[index];
}

// 平坦的旗标分发解析器（每个旗标一个分支），拆分反而降低可读性；
// 复杂度来自参数种类而非嵌套。
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool ParseArgs(int argc, char** argv, CliOptions& options, std::string& error) {
    if (argc < 2) {
        error = "缺少子命令（run/inject/save）";
        return false;
    }
    options.subcommand = argv[1];
    if (options.subcommand != "run" && options.subcommand != "inject" && options.subcommand != "save") {
        error = "未知子命令: " + options.subcommand;
        return false;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help" || key == "-h") {
            options.help = true;
            continue;
        }
        if (key == "--hash") {
            options.hash = true;
            options.has_hash = true;
            continue;
        }
        const bool known_value_flag = key == "--scenario" || key == "--seed" || key == "--threads" ||
                                      key == "--ticks" || key == "--out" || key == "--script" || key == "--ai-backend";
        if (!known_value_flag) {
            error = "未知参数: " + key;
            return false;
        }
        const char* value = NextValue(argc, argv, i, key, error);
        if (value == nullptr) {
            return false;
        }
        if (key == "--scenario") {
            options.scenario = value;
            continue;
        }
        if (key == "--seed") {
            std::uint64_t parsed = 0U;
            if (!ParseUint64(value, parsed)) {
                error = "非法 --seed 值: " + std::string(value);
                return false;
            }
            options.seed = parsed;
            continue;
        }
        if (key == "--threads") {
            int parsed = 0;
            if (!ParseInt(value, parsed)) {
                error = "非法 --threads 值: " + std::string(value);
                return false;
            }
            options.threads = parsed;
            continue;
        }
        if (key == "--ticks") {
            std::uint64_t parsed = 0U;
            if (!ParseUint64(value, parsed)) {
                error = "非法 --ticks 值: " + std::string(value);
                return false;
            }
            options.ticks = parsed;
            options.has_ticks = true;
            continue;
        }
        if (key == "--out") {
            options.out = value;
            continue;
        }
        if (key == "--script") {
            options.script = value;
            options.has_script = true;
            continue;
        }
        if (key == "--ai-backend") {
            options.ai_backend = value;
            options.has_ai_backend = true;
            continue;
        }
    }
    return true;
}

// 子命令旗标匹配校验：不适用于当前子命令的旗标显式报错（宪法 17：
// 不静默忽略用户输入）。
bool ValidateSubcommandFlags(const CliOptions& options, std::string& error) {
    if (options.subcommand == "run") {
        if (options.has_script) {
            error = "run 不支持 --script（命令脚本注入请使用 inject 子命令）";
            return false;
        }
    } else if (options.subcommand == "inject") {
        if (options.has_ai_backend) {
            error = "inject 不支持 --ai-backend（脚本 AI 请在 run 子命令中使用）";
            return false;
        }
    } else if (options.subcommand == "save") {
        if (options.has_ai_backend || options.has_ticks || options.has_hash || options.has_script) {
            error = "save 不支持 --ai-backend/--ticks/--hash/--script";
            return false;
        }
    }
    return true;
}

bool WriteEventsJsonl(const std::filesystem::path& path, const std::vector<wfs::sim::SimEvent>& events) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const wfs::sim::SimEvent& event : events) {
        out << nlohmann::json{{"seq", event.seq},
                              {"tick", event.tick},
                              {"category", std::string(wfs::sim::to_string(event.category))},
                              {"severity", std::string(wfs::sim::to_string(event.severity))},
                              {"message", event.message}}
                   .dump()
            << '\n';
    }
    out.close();
    return static_cast<bool>(out);
}

int RunSubcommand(const CliOptions& options) {
    const std::uint64_t seed = options.seed.value_or(0U);
    const int threads = options.threads.value_or(4);
    const std::uint64_t ticks = options.ticks.value_or(1200U);
    wfs::sim::HeadlessRunOptions run_options;
    run_options.scenario_path = options.scenario;
    run_options.seed = seed;
    run_options.threads = threads;
    run_options.ticks = ticks;
    run_options.ai_backend = options.ai_backend;
    const wfs::sim::HeadlessRunResult result = wfs::sim::run_headless(run_options);
    if (!result.ok()) {
        std::cerr << "sim_headless run: " << result.error << "\n";
        return 1;
    }
    if (!options.out.empty() && !WriteEventsJsonl(options.out, result.events)) {
        std::cerr << "sim_headless run: 无法写入事件输出: " << options.out.string() << "\n";
        return 1;
    }
    // 先落盘事件文件，成功后再输出哈希：失败不产生部分 stdout。
    if (options.hash) {
        std::cout << result.state_hash << '\n';
    }
    return 0;
}

int InjectSubcommand(const CliOptions& options) {
    const std::uint64_t seed = options.seed.value_or(0U);
    const int threads = options.threads.value_or(4);
    const std::uint64_t ticks = options.ticks.value_or(1200U);
    if (options.script.empty()) {
        std::cerr << "sim_headless: inject 需要 --script\n\n" << Usage();
        return 2;
    }
    wfs::sim::HeadlessInjectOptions inject_options;
    inject_options.scenario_path = options.scenario;
    inject_options.seed = seed;
    inject_options.threads = threads;
    inject_options.ticks = ticks;
    inject_options.script_path = options.script;
    const wfs::sim::HeadlessInjectResult result = wfs::sim::inject_headless(inject_options);
    if (!result.ok()) {
        std::cerr << "sim_headless inject: " << result.error << "\n";
        return 1;
    }
    if (!options.out.empty() && !WriteEventsJsonl(options.out, result.events)) {
        std::cerr << "sim_headless inject: 无法写入事件输出: " << options.out.string() << "\n";
        return 1;
    }
    if (options.hash) {
        std::cout << result.state_hash << '\n';
    }
    return 0;
}

int SaveSubcommand(const CliOptions& options) {
    const std::uint64_t seed = options.seed.value_or(0U);
    const int threads = options.threads.value_or(4);
    if (options.out.empty()) {
        std::cerr << "sim_headless: save 需要 --out\n\n" << Usage();
        return 2;
    }
    wfs::sim::HeadlessSaveOptions save_options;
    save_options.scenario_path = options.scenario;
    save_options.seed = seed;
    save_options.threads = threads;
    save_options.out_path = options.out;
    const wfs::sim::HeadlessSaveResult result = wfs::sim::save_headless(save_options);
    if (!result.ok()) {
        std::cerr << "sim_headless save: " << result.error << "\n";
        return 1;
    }
    return 0;
}

}  // namespace

// main 顶层 try/catch(...) 已兜底全部异常，仅错误处理路径自身的 IO 分配
// 可能再抛（不影响退出码语义）。
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    try {
        CliOptions options;
        std::string error;
        if (!ParseArgs(argc, argv, options, error)) {
            std::cerr << "sim_headless: " << error << "\n\n" << Usage();
            return 2;
        }
        if (options.help) {
            std::cout << Usage();
            return 0;
        }
        if (options.scenario.empty()) {
            std::cerr << "sim_headless: 缺少 --scenario\n\n" << Usage();
            return 2;
        }
        if (!ValidateSubcommandFlags(options, error)) {
            std::cerr << "sim_headless: " << error << "\n\n" << Usage();
            return 2;
        }

        if (options.subcommand == "run") {
            return RunSubcommand(options);
        }

        if (options.subcommand == "inject") {
            return InjectSubcommand(options);
        }

        return SaveSubcommand(options);
    } catch (const std::exception& exception) {
        std::cerr << "sim_headless: 内部错误: " << exception.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "sim_headless: 未知内部错误\n";
        return 1;
    }
}
