// tests/sim_tests/golden/golden_test.cpp
//
// T022：黄金确定性测试框架。
//
// 覆盖（quickstart §3.1；宪法第 7/15 条；SC-001/FR-025）：
// - 同输入两次运行哈希一致（含脚本 AI 决策注入路径）；
// - --threads 1 vs 4 哈希一致（线程数只影响性能，不影响状态哈希）；
// - 跨运行形态一致性：无头 CLI run --hash == 库内 run_headless ==
//   C ABI wfs_sim_create + step 的 wfs_sim_get_state_hash；
// - 固定黄金哈希文件（golden/golden-run.hash）比对：任何改变权威状态哈希
//   的变更必须在此显式登记，防止无意的确定性回归。
//
// 黄金哈希变更流程：先运行本测试获取新哈希，人工确认确定性语义未破坏后
// 更新 golden-run.hash 再提交（宪法 7：可复现性门禁）。

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "wfs/sim/c_api.h"
#include "wfs/sim/headless.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using wfs::sim::HeadlessRunOptions;
using wfs::sim::HeadlessRunResult;
using wfs::sim::run_headless;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path GoldenHashFile() {
    return RepoRoot() / "tests" / "sim_tests" / "golden" / "golden-run.hash";
}

std::string Exe() {
    return WFS_SIM_HEADLESS_EXE;
}

// UTF-8 → UTF-16（Windows 命令行）；非 Windows 分支仅作编译占位。
std::wstring Utf8ToWide(const std::string& text) {
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    }
    return wide;
#else
    return std::wstring(text.begin(), text.end());
#endif
}

std::string Trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1U);
}

std::string ReadFileTrimmed(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return Trim(buffer.str());
}

bool IsHex64(const std::string& hash) {
    if (hash.size() != 64U) {
        return false;
    }
    for (const char ch : hash) {
        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_lower_hex = ch >= 'a' && ch <= 'f';
        if (!is_digit && !is_lower_hex) {
            return false;
        }
    }
    return true;
}

// 运行无头 CLI 并把 stdout/stderr 重定向到临时文件后读取（--hash 的
// 64 位十六进制）；退出码非 0 时返回空串（测试断言会显式报错）。
// Windows 用 CreateProcessW 直接启动（CRT system() 对带引号长命令有解析
// 缺陷，且子进程环境不能依赖 shell 展开），非 Windows 退回 system + 重定向。
std::string RunCliHash(const std::vector<std::string>& extra_args) {
    static int counter = 0;
    const std::filesystem::path out =
        std::filesystem::temp_directory_path() / ("wfs-golden-" + std::to_string(++counter) + ".txt");
    std::string command = "\"" + Exe() + "\" run --scenario \"" + SampleScenario().string() + "\" --seed 42 --hash";
    for (const std::string& arg : extra_args) {
        command += " \"" + arg + "\"";
    }
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output_file = CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output_file == INVALID_HANDLE_VALUE) {
        return {};
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(STARTUPINFOW);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output_file;
    startup.hStdError = output_file;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring command_line = Utf8ToWide(command);
    const BOOL started = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &startup, &process);
    CloseHandle(output_file);
    if (!started) {
        return {};
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1U;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    const std::string text = ReadFileTrimmed(out);
    DeleteFileW(out.c_str());
    return exit_code == 0U ? text : std::string{};
#else
    command += " > \"" + out.string() + "\" 2>&1";
    const int exit_code = std::system(command.c_str());
    const std::string text = ReadFileTrimmed(out);
    std::error_code ignored;
    std::filesystem::remove(out, ignored);
    return exit_code == 0 ? text : std::string{};
#endif
}

// 与 CLI 完全同参数的库内驱动（跨运行形态一致性：同一实现路径）。
HeadlessRunResult RunDriver(int threads, const std::string& backend, std::uint64_t ticks) {
    HeadlessRunOptions options;
    options.scenario_path = SampleScenario();
    options.seed = 42U;
    options.threads = threads;
    options.ticks = ticks;
    options.ai_backend = backend;
    return run_headless(options);
}

std::vector<std::string> ScriptRunArgs() {
    return {"--threads", "1", "--ticks", "600", "--ai-backend", "script"};
}

}  // namespace

TEST(WfsGoldenTest, SameInputTwiceProducesSameCliHash) {
    const std::vector<std::string> args = ScriptRunArgs();
    const std::string first = RunCliHash(args);
    const std::string second = RunCliHash(args);
    EXPECT_TRUE(IsHex64(first)) << "CLI 哈希格式错误: " << first;
    EXPECT_EQ(first, second);
}

TEST(WfsGoldenTest, ThreadCountDoesNotAffectCliHash) {
    const std::string single = RunCliHash({"--threads", "1", "--ticks", "600", "--ai-backend", "script"});
    const std::string quad = RunCliHash({"--threads", "4", "--ticks", "600", "--ai-backend", "script"});
    EXPECT_TRUE(IsHex64(single));
    EXPECT_EQ(single, quad);
}

TEST(WfsGoldenTest, CliHashMatchesLibraryDriver) {
    const std::string cli = RunCliHash(ScriptRunArgs());
    const HeadlessRunResult driver = RunDriver(1, wfs::sim::kAiBackendScript, 600U);
    ASSERT_TRUE(driver.ok()) << driver.error;
    EXPECT_TRUE(IsHex64(cli)) << "CLI 哈希格式错误: " << cli;
    EXPECT_EQ(cli, driver.state_hash);
}

TEST(WfsGoldenTest, CliHashMatchesCAbiSimulation) {
    // 无 AI 模式：CLI 与 C ABI（create + step）必须得到同一状态哈希。
    const std::string cli = RunCliHash({"--threads", "4", "--ticks", "600", "--ai-backend", "none"});
    wfs_sim_handle* handle = wfs_sim_create(SampleScenario().string().c_str(), 42U, 4);
    ASSERT_NE(handle, nullptr);
    for (std::uint64_t i = 0U; i < 600U; ++i) {
        EXPECT_EQ(wfs_sim_step(handle), WFS_SIM_RESULT_OK);
    }
    char hash[WFS_SIM_STATE_HASH_HEX_LEN] = {};
    EXPECT_EQ(wfs_sim_get_state_hash(handle, hash), WFS_SIM_RESULT_OK);
    wfs_sim_destroy(handle);
    EXPECT_TRUE(IsHex64(cli)) << "CLI 哈希格式错误: " << cli;
    EXPECT_EQ(cli, std::string(hash));
}

TEST(WfsGoldenTest, GoldenHashMatchesRecordedFile) {
    // 黄金门禁：规范运行（seed 42 / threads 1 / 600 ticks / script AI）的哈希
    // 必须与仓库内 golden-run.hash 一致；不一致时按文件头注释流程显式更新。
    const HeadlessRunResult driver = RunDriver(1, wfs::sim::kAiBackendScript, 600U);
    ASSERT_TRUE(driver.ok()) << driver.error;
    const std::string golden = ReadFileTrimmed(GoldenHashFile());
    ASSERT_FALSE(golden.empty()) << "缺少黄金哈希文件: " << GoldenHashFile().string();
    EXPECT_EQ(driver.state_hash, golden);
}
