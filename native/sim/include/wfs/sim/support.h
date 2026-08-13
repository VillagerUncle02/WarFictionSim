// sim/include/wfs/sim/support.h
//
// T047：支援请求实体与状态机公开接口。
//
// 设计契约（data-model.md §14；FR-008/046；contracts/command-schema.md §5）：
// - 请求承载 目标（target_unit）、需求类型（request_type）、支援种类
//   （kinds）与请求方/受理方节点；命令来源（command_id）与脚本来源
//   （US3 裁决桩）共用同一 SupportRequest 形态，保证配属链逻辑复用。
// - 状态机：SUBMITTED → EVALUATING → EXECUTING/REJECTED；营级转请落到
//   ESCALATED，可由更上级重新进入 EVALUATING，最终必须给出明确结果
//   （SC-004：玩家始终收到明确响应）。终态不可再转移，非法转移返回
//   false 且不改状态（宪法第 17 条：显式报错，不静默吞错）。
// - id 唯一、提交顺序即确定性顺序（宪法第 7 条）；全部值类型支持
//   nlohmann::json 往返序列化（存档/快照复用，宪法第 13 条）。
// - 归建状态不挂在请求上：配属单位的 ASSIGNED/RETURNING/RETURNED 由
//   attach.h 记录（任务结束归建是配属记录的生命周期，data-model §14）。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim {

// 支援请求状态（data-model.md §14 状态机的请求侧投影）。
enum class SupportRequestState : std::uint8_t {
    kSubmitted = 0,   // 已提交，等待受理。
    kEvaluating = 1,  // 上级评估中。
    kExecuting = 2,   // 配属生效（力量临时移交）。
    kRejected = 3,    // 明确拒绝。
    kEscalated = 4,   // 向上转请（营级完整配属链）。
};

std::string_view to_string(SupportRequestState state) noexcept;
SupportRequestState support_request_state_from_string(std::string_view name);

// 状态机转移合法性（非法转移显式返回 false；调用方负责记录可见反馈）。
bool can_transition(SupportRequestState from, SupportRequestState to) noexcept;

// 支援请求：玩家命令（command_id 非空）与场景裁决桩（US3 TODO）共用。
struct SupportRequest {
    std::string id;                  // "req-<command_id>" 或脚本请求配置 id（唯一）。
    std::uint64_t seq = 0U;          // 到达序列号（仲裁的次关键字，FR-008）。
    std::int64_t priority = 0;       // 仲裁的主关键字（FR-008）。
    std::string command_id;          // 来源命令（命令链路）；脚本请求为空。
    std::string from_node;           // 请求方指挥节点。
    std::string to_node;             // 受理指挥节点。
    std::string target_unit;         // 需求目标（data-model §14）。
    std::string request_type;        // 需求类型（reinforce/fire_support/...）。
    std::vector<std::string> kinds;  // 支援种类（受所属编制资源池约束，FR-008）。
    std::uint64_t quantity = 1U;     // 请求数量（按每种支援种类计）。
    std::string for_command_id;      // 关联的任务命令（任务结束 → 归建）。
    // 裁决桩：任务结束的确定性替代（US3 接入下属任务上报后移除，见待办登记）。
    std::uint64_t return_after_ticks = 0U;
    std::uint64_t submitted_tick = 0U;
    std::uint64_t evaluating_tick = 0U;
    std::uint64_t resolved_tick = 0U;
    SupportRequestState state = SupportRequestState::kSubmitted;
    std::string resolution;                      // "assign" | "reject" | "escalate"。
    std::string resolution_reason;               // 稳定错误码（INSUFFICIENT_SCORE/...）。
    std::vector<std::string> assigned_unit_ids;  // 配属成功后的资源类型清单。

    bool operator==(const SupportRequest&) const = default;
};

void to_json(nlohmann::json& json, const SupportRequest& request);
void from_json(const nlohmann::json& json, SupportRequest& request);

// 支援请求登记表：提交顺序 = 确定性顺序，状态转移经状态机校验。
class SupportChain {
   public:
    // 提交请求：id 必须唯一且状态为 SUBMITTED；重复/非法返回 nullptr。
    SupportRequest* Submit(SupportRequest request);
    // 状态转移：非法转移返回 false 且请求状态不变。
    bool Transition(const std::string& id, SupportRequestState target);
    // 命令来源去重：同一命令只登记一次请求（step_support 扫描链路时使用）。
    bool HasCommand(const std::string& command_id) const;

    const SupportRequest* Find(const std::string& id) const;
    SupportRequest* FindMutable(const std::string& id);
    std::vector<SupportRequest> RequestsInSubmitOrder() const { return requests_; }
    std::size_t size() const noexcept { return requests_.size(); }
    bool empty() const noexcept { return requests_.empty(); }
    void Clear() noexcept { requests_.clear(); }

   private:
    friend void from_json(const nlohmann::json& json, SupportChain& chain);

    std::vector<SupportRequest> requests_;
};

void to_json(nlohmann::json& json, const SupportChain& chain);
// 反序列化校验：id 唯一 + 状态机合法（损坏数据显式拒绝，宪法 17）。
void from_json(const nlohmann::json& json, SupportChain& chain);

// 支援链路运行配置（数据驱动：场景 raw["support"]；宪法第 12 条）。
struct SupportConfig {
    // 作战规模："platoon" = 连排级有限分数（FR-005/008），"battalion" =
    // 营级完整配属链。v1 无旅级扮演，转请最终以确定性桩明确拒绝。
    std::string scale = "platoon";
    std::string faction_id;                      // data/factions/<id>.json（派系模板）。
    std::string player_node_id;                  // 请求方视角节点（连排级 = 玩家节点）。
    std::string superior_node_id;                // 受理/资源池上级节点（营）。
    std::uint64_t evaluation_delay_ticks = 20U;  // 评估延迟（含通讯后）。
    std::uint64_t return_delay_ticks = 40U;      // 归建途程延迟。
    std::uint64_t approval_step_ticks = 10U;     // 每级审批转发延迟。
    // 场景裁决桩请求（US3 营级 AI 接入前以确定性规则替代，登记 TODO）。
    std::vector<SupportRequest> scripted_requests;

    // 从场景 raw JSON 读取；非法值显式抛错（宪法 17，与既有 Config 一致）。
    static SupportConfig FromScenario(const nlohmann::json& raw);
};

}  // namespace wfs::sim
