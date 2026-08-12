// tests/sim_tests/test_temp_dir.h
//
// F1：测试临时路径唯一化共享设施。
//
// 背景：CTest 以 -jN 并行时，同一测试可执行文件会被并发启动多个进程；
// 旧实现按"进程内静态计数器"命名临时目录/文件，不同进程会复用同名路径，
// 相互删除/覆盖导致偶发失败（串行全绿、并行偶红）。
//
// 方案：所有测试临时路径统一为
//   <系统临时目录>/<前缀>-<进程 id>-<进程内单调序号>[.扩展名]
// 进程 id 隔离并发进程，单调序号隔离同进程内多次创建；目录/文件在
// 析构时清理（仅删除本工具创建且位于系统临时目录内的路径，防御误删）。

#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace wfs::sim::test {

// 当前进程 id（跨进程唯一性的基础）。
inline unsigned long ProcessId() {
#ifdef _WIN32
    return static_cast<unsigned long>(::_getpid());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

// 进程内单调序号（同一进程多次创建仍唯一）。
inline unsigned long long TempCounter() {
    static unsigned long long counter = 0;
    return counter++;
}

// 生成唯一临时文件路径（不创建文件，由调用方决定写入方式）。
inline std::filesystem::path UniqueTempFile(const std::string& prefix, const std::string& extension = ".tmp") {
    return std::filesystem::temp_directory_path() /
           (prefix + "-" + std::to_string(ProcessId()) + "-" + std::to_string(TempCounter()) + extension);
}

// 测试专用临时目录：<prefix>-<pid>-<seq>，析构时递归清理。
class TempDir {
   public:
    explicit TempDir(const std::string& prefix = "wfs-test") {
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(ProcessId()) + "-" + std::to_string(TempCounter()));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);  // 清理异常退出遗留的陈旧目录。
        std::filesystem::create_directories(path_);
    }

    ~TempDir() { Cleanup(); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    // 写入文件（自动创建父目录）；返回完整路径。
    std::filesystem::path Write(const std::string& name, const std::string& content) const {
        const std::filesystem::path file = path_ / name;
        if (!file.parent_path().empty()) {
            std::filesystem::create_directories(file.parent_path());
        }
        std::ofstream out(file, std::ios::binary);
        out << content;
        return file;
    }

   private:
    void Cleanup() {
        const std::filesystem::path temp_root = std::filesystem::temp_directory_path();
        const std::filesystem::path normalized = path_.lexically_normal();
        if (normalized.string().starts_with(temp_root.string())) {
            std::error_code ec;
            std::filesystem::remove_all(normalized, ec);
        }
    }

    std::filesystem::path path_;
};

}  // namespace wfs::sim::test
