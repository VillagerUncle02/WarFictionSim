// sim/include/wfs/sim/queue.h
//
// T011：事件/命令确定性队列（EventQueue）公开接口。
//
// 确定性契约（宪法第 7/16 条；spec FR-052）：
// - 玩家命令与 AI 决策（payload 为命令 JSON 文本）进入同一队列，按
//   (game_tick, 单调序列号) 排序；模拟核心按序取出执行，保证相同输入
//   产生相同处理顺序。
// - 序列号可由调用方按确定性规则显式提供（如注入通道记录的到达序号，
//   T020），也可由队列自动分配（全局单调、不重复）。同 tick 内先进先出
//   等价于按 seq 升序。
// - 重复 seq（同一事件在队列中只能存在一份）是确定性违约：抛
//   std::invalid_argument，入队失败不改变队列状态（宪法第 17 条：
//   显式报错，不静默吞错）。
// - 空队列 front()/pop() 抛 std::out_of_range；try_pop() 只弹出与请求
//   tick 匹配的最早事件，返回 false 表示不匹配或空，调用方据此推进 tick。
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
