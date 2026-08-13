// sim/include/wfs/sim/resource_pool.h
//
// T049：编制资源池与有限分数计算公开接口。
//
// 设计契约（FR-008；data-model.md §13/§14/§15；宪法第 7/9/12 条）：
// - 可用力量计算由确定性代码完成：全部纯函数，输入 = 派系资源池 + 已配属
//   清单 + 请求参数，不读时钟、不抽随机数、不接受 AI 直接修改输入
//   （AI 只能生成请求，规则校验后由本模块裁决，宪法第 9 条）。
// - 连排级有限分数：固定额度 support_score，按支援种类成本 × 数量扣分、
//   用尽即止（FR-008）；额度与可用支援种类由请求方所属营级编制（资源池）
//   确定。营级规模走完整配属链，不做分数扣减（limited_score=false）。
// - 请求范围约束：kinds 必须全部存在于资源池条目（SCOPE_VIOLATION），
//   数量必须为正；错误以结构化结果返回（错误码不抛异常策略沿用）。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "wfs/sim/faction.h"

namespace wfs::sim {

// 已配属（未归还）的资源占用：kind_id → 数量。
struct ResourceAllocation {
    std::string kind_id;
    std::uint64_t quantity = 0U;

    bool operator==(const ResourceAllocation&) const = default;
};

// 可用力量快照：扣减配属占用后的资源清单 + 剩余分数。
struct PoolSnapshot {
    std::uint64_t remaining_score = 0U;
    std::vector<ResourcePoolEntry> available;  // 数量为扣减后的可用值。
};

// 确定性可用力量计算：available.quantity = max(0, 池数量 - 已配属数量)。
PoolSnapshot available_force(const EchelonResourcePool& pool, const std::vector<ResourceAllocation>& assigned);

// 请求范围约束：kinds 非空且全部存在于资源池；数量为正。
struct ScopeCheckResult {
    bool ok = false;
    std::string error;  // SCOPE_VIOLATION（列出越界种类，确定性顺序）。
};

ScopeCheckResult check_request_scope(const EchelonResourcePool& pool, const std::vector<std::string>& kinds,
                                     std::uint64_t quantity);

// 有限分数扣减：cost = quantity × Σ(条目成本)；剩余不足返回 INSUFFICIENT_SCORE。
struct ScoreDeductionResult {
    bool ok = false;
    std::uint64_t cost = 0U;
    std::uint64_t remaining = 0U;
    std::string error;
};

ScoreDeductionResult deduct_score(const EchelonResourcePool& pool, std::uint64_t current_remaining,
                                  const std::vector<std::string>& kinds, std::uint64_t quantity);
// 快照重载：以快照剩余分数为基数扣减（attach 仲裁使用，成本来自快照条目）。
ScoreDeductionResult deduct_score(const PoolSnapshot& pool, const std::vector<std::string>& kinds,
                                  std::uint64_t quantity);

}  // namespace wfs::sim
