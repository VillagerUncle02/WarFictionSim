// sim/include/wfs/sim/attach.h
//
// T050：配属链仲裁与归建公开接口。
//
// 设计契约（FR-008/009；data-model.md §14；contracts/command-schema.md §3）：
// - 多个请求竞争同一资源时按（优先级降序, 到达序列号升序）确定性仲裁：
//   高优先级/先到者优先满足，其余拒绝或排队并明确反馈（FR-008）。
// - 裁决为确定性纯函数：可用力量计算（T049）+ 规则裁决桩。US3 接入营级
//   AI 决策前，配属/拒绝/转请的策略决定由规则桩实现（登记 TODO），
//   不接受 AI 直接修改状态（宪法第 9 条）。
// - 配属后临时移交指挥权，任务结束归建；归建途中的单位可被重新配属
//   （新请求优先于归建，原归建流程取消，FR-009）。
// - 途中补给/维修处理：补给/维修本体属 US3（FR-075/078），本模块只提供
//   确定性 hook 与结构化动作（登记 TODO）；瘫痪单位阻塞配属。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/resource_pool.h"
#include "wfs/sim/support.h"

namespace wfs::sim {

// 配属单位生命周期（data-model.md §14：任务结束归建）。
enum class AttachUnitState : std::uint8_t {
    kAssigned = 0,   // 已配属（临时移交指挥权）。
    kReturning = 1,  // 归建途中。
    kReturned = 2,   // 已归建（资源回到原属编制）。
};

// 裁决结果类型。
enum class AttachDecisionKind : std::uint8_t {
    kAssign = 0,    // 配属。
    kReject = 1,    // 明确拒绝。
    kEscalate = 2,  // 向上转请。
};

std::string_view to_string(AttachUnitState state) noexcept;
AttachUnitState attach_unit_state_from_string(std::string_view name);
std::string_view to_string(AttachDecisionKind kind) noexcept;
AttachDecisionKind attach_decision_kind_from_string(std::string_view name);

// 单请求裁决结果（确定性规则桩；US3 AI 接入前使用）。
struct AttachDecision {
    AttachDecisionKind kind = AttachDecisionKind::kReject;
    std::string reason;              // 稳定错误码/原因。
    std::vector<std::string> units;  // 配属资源类型清单（assign 时非空）。
    std::uint64_t score_cost = 0U;   // 连排级扣分（limited_score 时）。
    std::uint64_t score_remaining = 0U;

    bool operator==(const AttachDecision&) const = default;
};

// 配属记录：每个记录对应一种资源的配属/归建生命周期。
struct AttachRecord {
    std::string id;  // "att-<n>"（确定性单调）。
    std::string request_id;
    std::string kind_id;           // 资源类型（资源池条目 id）。
    std::string assigned_to_node;  // 接收方（请求方）节点。
    std::string parent_node;       // 原属节点（营）。
    AttachUnitState state = AttachUnitState::kAssigned;
    std::uint64_t assigned_tick = 0U;
    std::uint64_t return_start_tick = 0U;
    std::uint64_t returned_tick = 0U;
    // 途中补给/维修标记（US3 hook；FR-009/075/078）。
    bool needs_supply = false;
    bool needs_repair = false;
    bool immobilized = false;

    bool operator==(const AttachRecord&) const = default;
};

// 单请求裁决（确定性规则桩；US3 AI 决策接入后由决策经规则校验替代）。
// limited_score = 连排级有限分数模式（FR-005/008）；has_superior 决定
// 可用力量不足时转请还是拒绝。
AttachDecision adjudicate(const SupportRequest& request, const PoolSnapshot& pool, bool limited_score,
                          bool has_superior);

// 多请求竞争仲裁：按（优先级降序, 到达序列号升序）逐请求裁决，先满足者
// 优先占用资源与分数（FR-008）；assigned 与 remaining_score 随裁决推进。
std::vector<AttachDecision> arbitrate_requests(const std::vector<SupportRequest>& pending,
                                               const EchelonResourcePool& pool,
                                               const std::vector<ResourceAllocation>& assigned, bool limited_score,
                                               std::uint64_t remaining_score, bool has_superior);

// 途中补给/维修动作（US3 本体后置，hook 返回结构化动作）。
enum class TransitLogisticsAction : std::uint8_t {
    kProceed = 0,             // 可直接前往接收方。
    kSupplyThenProceed = 1,   // 先补给再出发（最近补给点/就地申请，US3）。
    kRepairThenProceed = 2,   // 战场抢修后转移（FR-009/078）。
    kBlockedHeavyRepair = 3,  // 需要大修/拖运：阻塞配属（不参与重新配属）。
};

std::string_view to_string(TransitLogisticsAction action) noexcept;

struct TransitLogisticsResult {
    TransitLogisticsAction action = TransitLogisticsAction::kProceed;
    std::string reason;
};

// 确定性途中处置：瘫痪（需拖运）阻塞配属；可抢修则修复后转移；补给
// 可达补给点先补给（不可达走就地申请 hook，US3 送达后续行）。
TransitLogisticsResult resolve_transit_logistics(const AttachRecord& record, bool supply_point_reachable,
                                                 bool field_repair_possible);

void to_json(nlohmann::json& json, const AttachDecision& decision);
void from_json(const nlohmann::json& json, AttachDecision& decision);
void to_json(nlohmann::json& json, const AttachRecord& record);
void from_json(const nlohmann::json& json, AttachRecord& record);

// 配属登记表：配属/归建/重新配属生命周期与占用统计。
class AttachRegistry {
   public:
    // 配属一种资源：id 自动分配 "att-<n>"，失败返回 nullptr。
    AttachRecord* Attach(const std::string& request_id, const std::string& kind_id, const std::string& assigned_to_node,
                         const std::string& parent_node, std::uint64_t tick);
    // 任务结束启动归建（RETURNING）；非法状态/未知 id 返回 false。
    bool StartReturn(const std::string& id, std::uint64_t tick);
    // 归建完成（RETURNED），资源回到原属（占用释放）。
    bool CompleteReturn(const std::string& id, std::uint64_t tick);
    // 归建途中重新配属：新请求优先于归建，原归建流程取消（FR-009）。
    // 瘫痪（需大修/拖运）单位阻塞配属，返回 false。
    bool Reassign(const std::string& id, const std::string& new_request_id, const std::string& new_assigned_to_node,
                  std::uint64_t tick);

    const AttachRecord* Find(const std::string& id) const;
    AttachRecord* FindMutable(const std::string& id);
    std::vector<AttachRecord> RecordsInInsertionOrder() const { return records_; }
    // 未归还占用（kAssigned + kReturning）：供资源池可用力量计算。
    std::vector<ResourceAllocation> ActiveAllocations() const;
    std::size_t size() const noexcept { return records_.size(); }
    bool empty() const noexcept { return records_.empty(); }
    void Clear() noexcept { records_.clear(); }

   private:
    friend void from_json(const nlohmann::json& json, AttachRegistry& registry);

    std::vector<AttachRecord> records_;
};

void to_json(nlohmann::json& json, const AttachRegistry& registry);
// 反序列化校验 id 唯一；损坏数据显式抛 std::invalid_argument（宪法 17）。
void from_json(const nlohmann::json& json, AttachRegistry& registry);

}  // namespace wfs::sim
