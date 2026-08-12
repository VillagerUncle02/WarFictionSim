// tests/sim_tests/parallel_test.cpp
//
// T018 单元测试：确定性分区并行框架。
// 覆盖稳定分桶（桶内原始顺序、空桶保留、越界/零桶报错）、1 线程 vs N 线程
// map/reduce 结果完全一致、固定桶序归约、空输入仍处理固定桶集、异常传播
// 与线程汇合，以及 worker 数边界（宪法第 7 条：线程数与调度不影响结果）。

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wfs/sim/parallel.h"

namespace {

using wfs::sim::DeterministicParallelExecutor;

std::vector<int> Range(int count) {
    std::vector<int> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        items.push_back(i);
    }
    return items;
}

std::string FirstOfBucketOrMinusOne(const std::vector<int>& bucket) {
    return bucket.empty() ? std::string("-1") : std::to_string(bucket.front());
}

void ConcatenateParts(std::string& acc, const std::string& part) {
    acc += part;
    acc += "|";
}

}  // namespace

TEST(WfsParallelTest, PartitionIsStableAndPreservesOriginalOrder) {
    const std::vector<int> items = Range(10);
    const auto bucket_of = [](int value) { return static_cast<std::size_t>(value % 4); };
    const std::vector<std::vector<int>> buckets = DeterministicParallelExecutor::partition(items, 4u, bucket_of);

    ASSERT_EQ(buckets.size(), 4u);
    EXPECT_EQ(buckets[0], (std::vector<int>{0, 4, 8}));
    EXPECT_EQ(buckets[1], (std::vector<int>{1, 5, 9}));
    EXPECT_EQ(buckets[2], (std::vector<int>{2, 6}));
    EXPECT_EQ(buckets[3], (std::vector<int>{3, 7}));

    // 重复调用/不同执行器实例产生相同分桶（调度无关）。
    EXPECT_EQ(DeterministicParallelExecutor::partition(items, 4u, bucket_of), buckets);
}

TEST(WfsParallelTest, PartitionKeepsEmptyBuckets) {
    const std::vector<int> items = {0, 1, 2};
    const auto bucket_of = [](int) { return std::size_t{0}; };
    const std::vector<std::vector<int>> buckets = DeterministicParallelExecutor::partition(items, 3u, bucket_of);
    ASSERT_EQ(buckets.size(), 3u);
    EXPECT_EQ(buckets[0], (std::vector<int>{0, 1, 2}));
    EXPECT_TRUE(buckets[1].empty());
    EXPECT_TRUE(buckets[2].empty());
}

TEST(WfsParallelTest, PartitionRejectsZeroBuckets) {
    const auto bucket_of = [](int) { return std::size_t{0}; };
    EXPECT_THROW(DeterministicParallelExecutor::partition(std::vector<int>{1}, 0u, bucket_of), std::invalid_argument);
}

TEST(WfsParallelTest, PartitionRejectsOutOfRangeBucket) {
    const auto bucket_of = [](int) { return std::size_t{99}; };
    EXPECT_THROW(DeterministicParallelExecutor::partition(std::vector<int>{1}, 2u, bucket_of), std::out_of_range);
}

TEST(WfsParallelTest, MapReduceIsIdenticalForOneAndManyWorkers) {
    const std::vector<int> items = Range(64);
    const auto bucket_of = [](int value) { return static_cast<std::size_t>((value * 7) % 5); };
    const auto task = [](const std::vector<int>& bucket) {
        std::string out;
        for (const int value : bucket) {
            out += std::to_string(value) + ",";
        }
        return out;
    };
    const auto reduce = [](std::string& acc, const std::string& part) { acc += "[" + part + "]"; };

    const DeterministicParallelExecutor single(1);
    const DeterministicParallelExecutor quad(4);
    const DeterministicParallelExecutor max(DeterministicParallelExecutor::kMaxWorkerCount);
    const std::string expected = single.map_reduce(items, 5u, bucket_of, task, std::string(), reduce);
    EXPECT_EQ(quad.map_reduce(items, 5u, bucket_of, task, std::string(), reduce), expected);
    EXPECT_EQ(max.map_reduce(items, 5u, bucket_of, task, std::string(), reduce), expected);
    EXPECT_EQ(single.map_reduce(items, 5u, bucket_of, task, std::string(), reduce), expected);
}

TEST(WfsParallelTest, MapReduceReducesInFixedBucketOrder) {
    const std::vector<int> items = Range(9);
    const auto bucket_of = [](int value) { return static_cast<std::size_t>(value % 3); };
    const DeterministicParallelExecutor single(1);
    const DeterministicParallelExecutor quad(4);

    const std::string single_result = single.map_reduce(items, 3u, bucket_of, FirstOfBucketOrMinusOne, std::string(),
                                                        [](std::string& acc, const std::string& part) { acc += part; });
    const std::string quad_result = quad.map_reduce(items, 3u, bucket_of, FirstOfBucketOrMinusOne, std::string(),
                                                    [](std::string& acc, const std::string& part) { acc += part; });

    // 桶 0 首元素 0、桶 1 首元素 1、桶 2 首元素 2：归约顺序固定为桶序号升序。
    EXPECT_EQ(single_result, "012");
    EXPECT_EQ(quad_result, "012");
}

TEST(WfsParallelTest, MapReduceProcessesFixedBucketSetForEmptyItems) {
    const std::vector<int> items;
    const auto bucket_of = [](int) { return std::size_t{0}; };
    const DeterministicParallelExecutor single(1);
    const DeterministicParallelExecutor quad(4);

    // 空输入也必须处理全部 bucket_count 个桶（固定桶集），归约结果确定。
    const std::string expected = "I0|0|0|";
    EXPECT_EQ(single.map_reduce(
                  items, 3u, bucket_of, [](const std::vector<int>& bucket) { return std::to_string(bucket.size()); },
                  std::string("I"), ConcatenateParts),
              expected);
    EXPECT_EQ(quad.map_reduce(
                  items, 3u, bucket_of, [](const std::vector<int>& bucket) { return std::to_string(bucket.size()); },
                  std::string("I"), ConcatenateParts),
              expected);
}

TEST(WfsParallelTest, MapReduceLargeInputDeterministicAcrossWorkers) {
    const std::vector<int> items = Range(1000);
    const auto bucket_of = [](int value) { return static_cast<std::size_t>(value % 16); };
    const auto task = [](const std::vector<int>& bucket) {
        std::uint64_t sum = 0U;
        for (const int value : bucket) {
            sum += static_cast<std::uint64_t>(value);
        }
        return sum;
    };
    const auto reduce = [](std::uint64_t& acc, std::uint64_t part) { acc += part; };

    const DeterministicParallelExecutor single(1);
    const DeterministicParallelExecutor max(DeterministicParallelExecutor::kMaxWorkerCount);
    EXPECT_EQ(single.map_reduce(items, 16u, bucket_of, task, std::uint64_t{0}, reduce), 499500u);
    EXPECT_EQ(max.map_reduce(items, 16u, bucket_of, task, std::uint64_t{0}, reduce), 499500u);
}

TEST(WfsParallelTest, TaskExceptionPropagatesAfterJoinAndExecutorRemainsUsable) {
    const std::vector<int> items = Range(8);
    const auto bucket_of = [](int value) { return static_cast<std::size_t>(value % 4); };
    const DeterministicParallelExecutor quad(4);
    const auto throwing_task = [](const std::vector<int>& bucket) -> std::string {
        if (!bucket.empty() && bucket.front() % 4u == 1u) {
            throw std::runtime_error("bucket task failed");
        }
        return "ok";
    };

    EXPECT_THROW(quad.map_reduce(items, 4u, bucket_of, throwing_task, std::string(),
                                 [](std::string& acc, const std::string& part) { acc += part; }),
                 std::runtime_error);
    // 线程已汇合：同一执行器后续调用仍可正常工作（无死锁/线程泄漏）。
    const std::string result = quad.map_reduce(
        items, 4u, bucket_of, [](const std::vector<int>& bucket) { return std::to_string(bucket.size()); },
        std::string(), [](std::string& acc, const std::string& part) { acc += part; });
    EXPECT_EQ(result, "2222");
}

TEST(WfsParallelTest, WorkerCountValidationAndBounds) {
    EXPECT_THROW(DeterministicParallelExecutor(0u), std::invalid_argument);
    EXPECT_THROW(DeterministicParallelExecutor(9u), std::invalid_argument);
    EXPECT_EQ(DeterministicParallelExecutor().worker_count(), 4u);
    EXPECT_EQ(DeterministicParallelExecutor(1u).worker_count(), 1u);
    EXPECT_EQ(DeterministicParallelExecutor(8u).worker_count(), 8u);
}
