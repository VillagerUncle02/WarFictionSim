// sim/include/wfs/sim/queue.h
//
// T011：事件/命令确定性队列（EventQueue）公开接口。
//
// 确定性契约（宪法第 7/16 条；spec FR-052）：
// - 玩家命令与 AI 决策（payload 为命令 JSON 文本）进入同一队列，按
//   (game_tick, 单调序列号) 排序；模拟核心按序取出执行，保证相同输入
//   产生相同处理顺序。
// - 序列号可由调用方按确定性规则显式提供（如注入通道记录的到达序号，
//   T020），也可由队列自动分配。两者可混用：显式入队成功后自动游标推进到
//   max(next_seq_, seq + 1)（饱和），保证 auto 序列号全局单调、不回收；
//   auto 分配还会跳过队列中已占用的 seq（确定性递增，基于现有存储查重），
//   因此显式 seq 0 不会锁死 auto 分配；两者共享同一唯一性校验。
//   同 tick 内先进先出等价于按 seq 升序。
// - auto 序列号空间耗尽（游标停在 UINT64_MAX 且该值已被占用）时抛
//   std::overflow_error，绝不回绕；显式 seq 不受影响，仍可入队未占用值。
// - 重复 seq（同一事件在队列中只能存在一份）是确定性违约：抛
//   std::invalid_argument，入队失败不改变队列状态（宪法第 17 条：
//   显式报错，不静默吞错）。
// - 空队列 front()/pop() 抛 std::out_of_range；try_pop() 弹出
//   tick <= 请求 tick 的最早事件：请求 tick 大于事件 tick 时按补发语义
//   弹出（不丢命令），事件 tick 晚于请求 tick 时返回 false。
// - 非线程安全：并发注入/弹出必须由调用方串行化（AI 注入边界由 T020 落实）。
//
// 自动分配的序列号从 0 开始单调递增，不回收；序列号空间为 uint64。

#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_set>

#include "wfs/sim/clock.h"

namespace wfs::sim {

struct QueuedEvent {
    GameTick tick;        // 应执行的游戏 tick。
    std::uint64_t seq;    // 全局单调序列号（同一队列内唯一）。
    std::string payload;  // 事件/命令纯数据（命令 JSON 文本）。
};

class EventQueue {
   public:
    EventQueue() = default;

    std::size_t size() const noexcept;
    bool empty() const noexcept;

    // 自动分配下一个单调序列号并返回。
    std::uint64_t enqueue(GameTick tick, std::string payload);
    // 显式提供序列号（调用方按确定性规则分配）。
    void enqueue(GameTick tick, std::uint64_t seq, std::string payload);

    const QueuedEvent& front() const;
    void pop();
    bool try_pop(GameTick tick, QueuedEvent& out);

    std::uint64_t next_seq() const noexcept;

   private:
    void insert(GameTick tick, std::uint64_t seq, std::string payload);

    struct ByTickThenSeq {
        bool operator()(const QueuedEvent& lhs, const QueuedEvent& rhs) const {
            if (lhs.tick != rhs.tick) {
                return lhs.tick < rhs.tick;
            }
            return lhs.seq < rhs.seq;
        }
    };

    std::set<QueuedEvent, ByTickThenSeq> events_;
    std::unordered_set<std::uint64_t> seqs_;
    std::uint64_t next_seq_ = 0u;
};

}  // namespace wfs::sim
