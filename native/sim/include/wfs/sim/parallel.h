// sim/include/wfs/sim/parallel.h
//
// T018：确定性分区并行框架公开接口。
//
// 设计契约（research.md §1；宪法第 7 条；spec FR-025/Assumptions）：
// - 分桶是"按空间区域/实体分桶"的确定性骨架：bucket_count 由调用方固定
//   （与线程数无关），partition() 按 bucket_of(item) 稳定分桶——桶内保持
//   原始顺序、空桶保留、同一输入必然产生同一分桶。
// - map_reduce()：所有桶任务并行执行（worker_count 只控制并行度，
//   取值 1..kMaxWorkerCount），全部完成后（固定边界同步点）再按桶序号
//   升序固定顺序归约；跨桶交互不在此框架内直接结算，由上层在边界同步点
//   统一收集。因此 1 线程与 N 线程产生完全相同的最终结果（宪法第 7 条）。
// - worker_count 不参与任何结果计算、不进入状态哈希（T016 联动）。
// - 异常安全：桶任务抛异常时，所有工作线程先汇合（join）再重抛第一个
//   异常，不泄漏线程、不死锁；executor 可继续复用。
// - Result 要求：可默认构造、可移动/拷贝赋值（bucket 结果数组元素）；
//   reduce 形如 void(Result&, const Result&)，按桶序号升序调用。
//
// 线程安全性：executor 自身无可变状态，可重复调用；并发调用同一实例由
// 调用方串行化。

#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wfs::sim {

namespace detail {

// 并行执行 bucket_count 个编号任务（实现见 parallel.cpp）：
// 任务间无共享可变状态，调度顺序不参与结果；全部完成后返回。
void run_bucket_tasks(std::size_t worker_count, std::size_t bucket_count, const std::function<void(std::size_t)>& task);

}  // namespace detail

class DeterministicParallelExecutor {
   public:
    static constexpr std::size_t kDefaultWorkerCount = 4U;
    static constexpr std::size_t kMaxWorkerCount = 8U;

    // worker_count 必须在 [1, kMaxWorkerCount]，否则抛 std::invalid_argument。
    explicit DeterministicParallelExecutor(std::size_t worker_count = kDefaultWorkerCount);

    std::size_t worker_count() const noexcept;

    // 稳定分桶：bucket_of(item) 必须返回 [0, bucket_count)，越界抛
    // std::out_of_range；bucket_count 必须 >= 1。结果按桶序号排列。
    template <typename Item, typename BucketOf>
    static std::vector<std::vector<Item>> partition(const std::vector<Item>& items, std::size_t bucket_count,
                                                    BucketOf&& bucket_of) {
        if (bucket_count == 0U) {
            throw std::invalid_argument("wfs::sim::DeterministicParallelExecutor: bucket_count must be >= 1");
        }
        std::vector<std::vector<Item>> buckets(bucket_count);
        for (const Item& item : items) {
            const std::size_t bucket = bucket_of(item);
            if (bucket >= bucket_count) {
                throw std::out_of_range(
                    "wfs::sim::DeterministicParallelExecutor: bucket_of returned out-of-range bucket");
            }
            buckets[bucket].push_back(item);
        }
        return buckets;
    }

    // 并行 map + 固定顺序归约：先分桶，再并行执行每桶 task，边界同步后
    // 按桶序号升序归约。worker_count 只影响并行度，不影响结果。
    template <typename Item, typename BucketOf, typename Task, typename Result, typename Reduce>
    Result map_reduce(const std::vector<Item>& items, std::size_t bucket_count, BucketOf&& bucket_of, Task&& task,
                      Result initial, Reduce&& reduce) const {
        const std::vector<std::vector<Item>> buckets = partition(items, bucket_count, bucket_of);
        std::vector<Result> bucket_results(bucket_count);
        detail::run_bucket_tasks(worker_count_, bucket_count, [&](std::size_t bucket_index) {
            bucket_results[bucket_index] = task(buckets[bucket_index]);
        });
        Result combined = std::move(initial);
        for (std::size_t bucket_index = 0U; bucket_index < bucket_count; ++bucket_index) {
            reduce(combined, bucket_results[bucket_index]);
        }
        return combined;
    }

   private:
    std::size_t worker_count_;
};

}  // namespace wfs::sim
