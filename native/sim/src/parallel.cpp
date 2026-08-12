// sim/src/parallel.cpp
//
// T018：确定性分区并行框架实现。
//
// 实现策略（research.md §1，宪法第 7 条）：
// - 分桶与固定顺序归约在 parallel.h 模板中完成（不依赖线程）；本文件只
//   负责"并行执行桶任务"这一部分：用一个原子游标让工作线程无竞争地领取
//   桶编号，任务间无共享可变状态，因此调度顺序不影响结果。
// - 固定边界同步：所有桶任务完成后（join 全部工作线程）才返回，由调用方
//   在返回后按桶序号升序归约；跨桶交互必须等全部桶结果就绪。
// - 异常安全：任一桶任务抛异常时记录第一个异常并让该线程退出；主线程
//   等待所有工作线程 join 后重抛，保证不泄漏线程、不产生未定义行为。
// - worker_count 只决定创建多少工作线程，不参与任何结果计算。

#include "wfs/sim/parallel.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace wfs::sim {

DeterministicParallelExecutor::DeterministicParallelExecutor(std::size_t worker_count) : worker_count_(worker_count) {
    if (worker_count_ == 0U || worker_count_ > kMaxWorkerCount) {
        throw std::invalid_argument(
            "wfs::sim::DeterministicParallelExecutor: worker_count must be in [1, kMaxWorkerCount]");
    }
}

std::size_t DeterministicParallelExecutor::worker_count() const noexcept {
    return worker_count_;
}

namespace detail {

void run_bucket_tasks(std::size_t worker_count, std::size_t bucket_count,
                      const std::function<void(std::size_t)>& task) {
    const std::size_t worker_count_effective = std::min(worker_count, bucket_count);
    if (worker_count_effective == 0U) {
        return;
    }

    // 原子游标领取桶编号：每个桶恰好执行一次，执行顺序与线程调度无关。
    std::atomic<std::size_t> next_bucket{0U};
    std::mutex error_mutex;
    std::exception_ptr first_error;
    const auto worker = [&]() {
        for (;;) {
            const std::size_t bucket_index = next_bucket.fetch_add(1U);
            if (bucket_index >= bucket_count) {
                return;
            }
            try {
                task(bucket_index);
            } catch (...) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (first_error == nullptr) {
                    first_error = std::current_exception();
                }
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count_effective);
    for (std::size_t i = 0U; i < worker_count_effective; ++i) {
        workers.emplace_back(worker);
    }
    for (std::thread& worker_thread : workers) {
        worker_thread.join();
    }
    if (first_error != nullptr) {
        std::rethrow_exception(first_error);
    }
}

}  // namespace detail

}  // namespace wfs::sim
