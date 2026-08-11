// sim/src/queue.cpp
//
// T011：事件/命令确定性队列（EventQueue）实现。
//
// 实现策略：std::set 以 (tick, seq) 为键，保证任意乱序插入后仍按确定性
// 顺序弹出；额外维护 seq 集合做全局唯一性校验。auto-enqueue 与显式 seq
// 共享同一校验路径，保证两种入口的序列号语义一致；校验失败先于任何
// 状态修改执行，队列保持不变。try_pop() 采用补发语义：请求 tick 大于
// 事件 tick 时仍弹出（不丢命令），事件晚于请求 tick 时留待后续。

#include <stdexcept>
#include <string>
#include <utility>

#include "wfs/sim/queue.h"

namespace wfs::sim {

namespace {

[[noreturn]] void ThrowDuplicateSeq(std::uint64_t seq) {
    throw std::invalid_argument("wfs::sim::EventQueue: duplicate sequence number: " + std::to_string(seq));
}

}  // namespace

std::size_t EventQueue::size() const noexcept {
    return events_.size();
}

bool EventQueue::empty() const noexcept {
    return events_.empty();
}

std::uint64_t EventQueue::enqueue(GameTick tick, std::string payload) {
    // 确定性递增直到空闲：显式 seq 占用 0 时 auto 自动从下一个空闲值继续，
    // 不会因重复报错锁死；查找基于现有存储（seqs_），不依赖调用方约定。
    while (seqs_.contains(next_seq_)) {
        ++next_seq_;
    }
    const std::uint64_t seq = next_seq_;
    enqueue(tick, seq, std::move(payload));
    ++next_seq_;
    return seq;
}

void EventQueue::enqueue(GameTick tick, std::uint64_t seq, std::string payload) {
    if (!seqs_.insert(seq).second) {
        ThrowDuplicateSeq(seq);
    }
    const auto [it, inserted] = events_.emplace(QueuedEvent{tick, seq, std::move(payload)});
    if (!inserted) {
        seqs_.erase(seq);
        ThrowDuplicateSeq(seq);
    }
}

const QueuedEvent& EventQueue::front() const {
    if (events_.empty()) {
        throw std::out_of_range("wfs::sim::EventQueue: front() on empty queue");
    }
    return *events_.begin();
}

void EventQueue::pop() {
    if (events_.empty()) {
        throw std::out_of_range("wfs::sim::EventQueue: pop() on empty queue");
    }
    const auto it = events_.begin();
    seqs_.erase(it->seq);
    events_.erase(it);
}

bool EventQueue::try_pop(GameTick tick, QueuedEvent& out) {
    if (events_.empty()) {
        return false;
    }
    const auto it = events_.begin();
    if (it->tick > tick) {
        // 事件晚于请求 tick：留待后续按序补发。
        return false;
    }
    out = *it;
    seqs_.erase(it->seq);
    events_.erase(it);
    return true;
}

std::uint64_t EventQueue::next_seq() const noexcept {
    return next_seq_;
}

}  // namespace wfs::sim
