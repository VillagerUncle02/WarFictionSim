// tests/sim_tests/queue_test.cpp
//
// T011 单元测试：事件/命令确定性队列（EventQueue）。
// 覆盖 (tick, seq) 排序、同 tick FIFO、乱序插入、空队列行为、
// 序列号自动分配单调、重复 seq 拒绝且不改变队列状态、按 tick 弹出。
// 玩家命令与 AI 决策共用同一队列，顺序完全由 (tick, seq) 决定。

#include <cstdint>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "wfs/sim/queue.h"

namespace {

using std::string;
using std::uint64_t;
using wfs::sim::EventQueue;
using wfs::sim::QueuedEvent;

QueuedEvent Next(EventQueue& queue) {
    const QueuedEvent event = queue.front();
    queue.pop();
    return event;
}

}  // namespace

TEST(WfsQueueTest, EmptyQueueBehaviors) {
    EventQueue queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_THROW(queue.front(), std::out_of_range);
    EXPECT_THROW(queue.pop(), std::out_of_range);

    QueuedEvent out;
    EXPECT_FALSE(queue.try_pop(0u, out));
}

TEST(WfsQueueTest, OrdersByTickThenSeq) {
    EventQueue queue;
    queue.enqueue(3u, 7u, "t3s7");
    queue.enqueue(1u, 3u, "t1s3");
    queue.enqueue(2u, 5u, "t2s5");
    queue.enqueue(1u, 1u, "t1s1");

    EXPECT_EQ(Next(queue).payload, "t1s1");
    EXPECT_EQ(Next(queue).payload, "t1s3");
    EXPECT_EQ(Next(queue).payload, "t2s5");
    EXPECT_EQ(Next(queue).payload, "t3s7");
    EXPECT_TRUE(queue.empty());
}

TEST(WfsQueueTest, SameTickFifoBySequence) {
    EventQueue queue;
    // 故意按非序列号顺序插入，弹出仍按 seq 升序。
    queue.enqueue(4u, 2u, "s2");
    queue.enqueue(4u, 0u, "s0");
    queue.enqueue(4u, 3u, "s3");
    queue.enqueue(4u, 1u, "s1");
    queue.enqueue(4u, 4u, "s4");

    string order;
    while (!queue.empty()) {
        order += Next(queue).payload;
    }
    EXPECT_EQ(order, "s0s1s2s3s4");
}

TEST(WfsQueueTest, OutOfOrderInsertionStillSorted) {
    EventQueue queue;
    // 乱序插入：tick 先大后小、seq 交错。
    queue.enqueue(10u, 2u, "a");
    queue.enqueue(1u, 1u, "b");
    queue.enqueue(5u, 3u, "c");
    queue.enqueue(1u, 0u, "d");
    queue.enqueue(7u, 4u, "e");

    string order;
    while (!queue.empty()) {
        order += Next(queue).payload;
    }
    EXPECT_EQ(order, "dbcea");  // (1,0) d → (1,1) b → (5,2) c → (7,3) e → (10,0) a。
}

TEST(WfsQueueTest, AutoAssignedSeqIsMonotonicAndOrdered) {
    EventQueue queue;
    const uint64_t seq_first = queue.enqueue(2u, "first");
    const uint64_t seq_second = queue.enqueue(1u, "second");
    const uint64_t seq_third = queue.enqueue(1u, "third");
    EXPECT_EQ(seq_first, 0u);
    EXPECT_EQ(seq_second, 1u);
    EXPECT_EQ(seq_third, 2u);

    string order;
    while (!queue.empty()) {
        order += Next(queue).payload;
    }
    EXPECT_EQ(order, "secondthirdfirst");  // tick 1 内按入队顺序：second 先于 third。
    EXPECT_EQ(queue.next_seq(), 3u);
}

TEST(WfsQueueTest, PlayerAndAiInputsShareDeterministicOrder) {
    EventQueue queue;
    queue.enqueue(1u, 0u, "ai:decision");
    queue.enqueue(1u, 1u, "player:command");
    queue.enqueue(2u, 2u, "ai:decision");
    queue.enqueue(3u, 3u, "player:command");

    EXPECT_EQ(Next(queue).payload, "ai:decision");
    EXPECT_EQ(Next(queue).payload, "player:command");
    EXPECT_EQ(Next(queue).payload, "ai:decision");
    EXPECT_EQ(Next(queue).payload, "player:command");
    EXPECT_TRUE(queue.empty());
}

TEST(WfsQueueTest, DuplicateSeqRejectedWithoutStateChange) {
    EventQueue queue;
    queue.enqueue(3u, 7u, "original");
    EXPECT_THROW(queue.enqueue(3u, 7u, "duplicate-same-tick"), std::invalid_argument);
    EXPECT_THROW(queue.enqueue(4u, 7u, "duplicate-other-tick"), std::invalid_argument);

    EXPECT_EQ(queue.size(), 1u);
    const QueuedEvent& front = queue.front();
    EXPECT_EQ(front.tick, 3u);
    EXPECT_EQ(front.seq, 7u);
    EXPECT_EQ(front.payload, "original");
}

TEST(WfsQueueTest, ExplicitAndAutoSeqShareUniqueness) {
    EventQueue queue;
    queue.enqueue(0u, 5u, "explicit");
    const uint64_t auto_seq = queue.enqueue(0u, "auto");
    EXPECT_EQ(auto_seq, 0u);
    EXPECT_THROW(queue.enqueue(0u, 0u, "duplicate-of-auto"), std::invalid_argument);
    EXPECT_THROW(queue.enqueue(0u, 5u, "duplicate-of-explicit"), std::invalid_argument);
    EXPECT_EQ(queue.size(), 2u);
}

TEST(WfsQueueTest, PopRemovesFrontEvent) {
    EventQueue queue;
    queue.enqueue(1u, 0u, "a");
    queue.enqueue(2u, 1u, "b");
    queue.pop();
    EXPECT_EQ(queue.front().payload, "b");
    queue.pop();
    EXPECT_TRUE(queue.empty());
}

TEST(WfsQueueTest, TryPopOnlyMatchesRequestedTick) {
    EventQueue queue;
    queue.enqueue(2u, 0u, "at2");
    queue.enqueue(5u, 1u, "at5");

    QueuedEvent out;
    EXPECT_TRUE(queue.try_pop(2u, out));
    EXPECT_EQ(out.tick, 2u);
    EXPECT_EQ(out.seq, 0u);
    EXPECT_EQ(out.payload, "at2");

    // 最早事件 tick=5：请求其他 tick 不得弹出。
    EXPECT_FALSE(queue.try_pop(3u, out));
    EXPECT_FALSE(queue.try_pop(4u, out));
    EXPECT_TRUE(queue.try_pop(5u, out));
    EXPECT_EQ(out.payload, "at5");
    EXPECT_TRUE(queue.empty());
}
