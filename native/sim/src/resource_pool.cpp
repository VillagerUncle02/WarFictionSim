// sim/src/resource_pool.cpp
//
// T049：编制资源池与有限分数计算实现。
//
// 实现策略：全部为纯函数（宪法第 7/9 条），按输入参数显式计算：
// - available_force 按条目 id 匹配已配属清单扣减数量（下限 0），条目顺序
//   保持资源池声明顺序，保证输出确定；
// - deduct_score 固定顺序求和成本（kinds 顺序决定求和顺序，请求语义上
//   kinds 为无序集合，但同一输入顺序必然同一结果），剩余不足显式失败；
// - check_request_scope 只做范围约束（种类存在性 + 数量为正），可用数量
//   由 attach 仲裁层结合配属占用判断（分层职责：范围 vs 可用性）。

#include "wfs/sim/resource_pool.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace wfs::sim {

namespace {

const ResourcePoolEntry* FindEntry(const EchelonResourcePool& pool, const std::string& id) {
    const auto found = std::find_if(pool.entries.begin(), pool.entries.end(),
                                    [&](const ResourcePoolEntry& entry) { return entry.id == id; });
    return found == pool.entries.end() ? nullptr : &*found;
}

std::uint64_t AllocatedQuantity(const std::vector<ResourceAllocation>& assigned, const std::string& kind_id) {
    std::uint64_t total = 0U;
    for (const ResourceAllocation& allocation : assigned) {
        if (allocation.kind_id == kind_id) {
            total += allocation.quantity;
        }
    }
    return total;
}

}  // namespace

PoolSnapshot available_force(const EchelonResourcePool& pool, const std::vector<ResourceAllocation>& assigned) {
    PoolSnapshot snapshot;
    snapshot.remaining_score = pool.support_score;
    snapshot.available.reserve(pool.entries.size());
    for (const ResourcePoolEntry& entry : pool.entries) {
        const std::uint64_t used = AllocatedQuantity(assigned, entry.id);
        ResourcePoolEntry available = entry;
        available.quantity = used >= entry.quantity ? 0U : entry.quantity - used;
        snapshot.available.push_back(available);
    }
    return snapshot;
}

ScopeCheckResult check_request_scope(const EchelonResourcePool& pool, const std::vector<std::string>& kinds,
                                     const std::uint64_t quantity) {
    if (kinds.empty()) {
        return ScopeCheckResult{false, "SCOPE_VIOLATION kinds 为空"};
    }
    if (quantity == 0U) {
        return ScopeCheckResult{false, "SCOPE_VIOLATION quantity 必须为正"};
    }
    std::vector<std::string> missing;
    for (const std::string& kind : kinds) {
        if (FindEntry(pool, kind) == nullptr) {
            missing.push_back(kind);
        }
    }
    if (!missing.empty()) {
        std::string message = "SCOPE_VIOLATION 支援种类不在所属编制资源池内: ";
        for (std::size_t i = 0U; i < missing.size(); ++i) {
            if (i > 0U) {
                message += ",";
            }
            message += missing[i];
        }
        return ScopeCheckResult{false, std::move(message)};
    }
    return ScopeCheckResult{true, ""};
}

ScoreDeductionResult deduct_score(const EchelonResourcePool& pool, const std::uint64_t current_remaining,
                                  const std::vector<std::string>& kinds, const std::uint64_t quantity) {
    if (kinds.empty() || quantity == 0U) {
        return ScoreDeductionResult{false, 0U, current_remaining, "kinds 为空或数量为 0"};
    }
    std::uint64_t cost = 0U;
    for (const std::string& kind : kinds) {
        const ResourcePoolEntry* entry = FindEntry(pool, kind);
        if (entry == nullptr) {
            return ScoreDeductionResult{false, 0U, current_remaining,
                                        "支援种类不在资源池内: " + kind};
        }
        cost += entry->cost * quantity;
    }
    if (cost > current_remaining) {
        return ScoreDeductionResult{false, cost, current_remaining, "INSUFFICIENT_SCORE"};
    }
    return ScoreDeductionResult{true, cost, current_remaining - cost, ""};
}

ScoreDeductionResult deduct_score(const PoolSnapshot& pool, const std::vector<std::string>& kinds,
                                  const std::uint64_t quantity) {
    if (kinds.empty() || quantity == 0U) {
        return ScoreDeductionResult{false, 0U, pool.remaining_score, "kinds 为空或数量为 0"};
    }
    std::uint64_t cost = 0U;
    for (const std::string& kind : kinds) {
        const auto found = std::find_if(pool.available.begin(), pool.available.end(),
                                        [&](const ResourcePoolEntry& entry) { return entry.id == kind; });
        if (found == pool.available.end()) {
            return ScoreDeductionResult{false, 0U, pool.remaining_score, "支援种类不在资源池内: " + kind};
        }
        cost += found->cost * quantity;
    }
    if (cost > pool.remaining_score) {
        return ScoreDeductionResult{false, cost, pool.remaining_score, "INSUFFICIENT_SCORE"};
    }
    return ScoreDeductionResult{true, cost, pool.remaining_score - cost, ""};
}

}  // namespace wfs::sim
