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
    try {
        std::size_t consumed = 0U;
        value = std::stoull(text, &consumed, 10);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& text, int& value) {
    try {
        std::size_t consumed = 0U;
        value = std::stoi(text, &consumed, 10);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

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
        const auto next_value = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                error = "缺少参数值: " + name;
                return nullptr;
            }
            return argv[++i];
        };
        if (key == "--help" || key == "-h") {
            options.help = true;
        } else if (key == "--hash") {
            options.hash = true;
        } else if (key == "--scenario") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            options.scenario = value;
        } else if (key == "--seed") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            std::uint64_t parsed = 0U;
            if (!ParseUint64(value, parsed)) {
                error = "非法 --seed 值: " + std::string(value);
                return false;
            }
            options.seed = parsed;
        } else if (key == "--threads") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            int parsed = 0;
            if (!ParseInt(value, parsed)) {
                error = "非法 --threads 值: " + std::string(value);
                return false;
            }
            options.threads = parsed;
        } else if (key == "--ticks") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            std::uint64_t parsed = 0U;
            if (!ParseUint64(value, parsed)) {
                error = "非法 --ticks 值: " + std::string(value);
                return false;
            }
            options.ticks = parsed;
        } else if (key == "--out") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            options.out = value;
        } else if (key == "--script") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            options.script = value;
        } else if (key == "--ai-backend") {
            const char* value = next_value(key);
            if (value == nullptr) return false;
            options.ai_backend = value;
        } else {
            error = "未知参数: " + key;
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

}  // namespace

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

        const std::uint64_t seed = options.seed.value_or(0U);
        const int threads = options.threads.value_or(4);
        const std::uint64_t ticks = options.ticks.value_or(1200U);

        if (options.subcommand == "run") {
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
            if (options.hash) {
                std::cout << result.state_hash << '\n';
            }
            if (!options.out.empty() && !WriteEventsJsonl(options.out, result.events)) {
                std::cerr << "sim_headless run: 无法写入事件输出: " << options.out.string() << "\n";
                return 1;
            }
            return 0;
        }

        if (options.subcommand == "inject") {
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
            if (options.hash) {
                std::cout << result.state_hash << '\n';
            }
            if (!options.out.empty() && !WriteEventsJsonl(options.out, result.events)) {
                std::cerr << "sim_headless inject: 无法写入事件输出: " << options.out.string() << "\n";
                return 1;
            }
            return 0;
        }

        // save。
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
    } catch (const std::exception& exception) {
        std::cerr << "sim_headless: 内部错误: " << exception.what() << "\n";
        return 1;
    }
}
