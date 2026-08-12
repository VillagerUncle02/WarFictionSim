// sim/include/wfs/sim/command_chain.h
//
// T029：命令下达与通讯延迟链路公开接口。
//
// 设计契约（FR-030/040/041/045/046；contracts/command-schema.md §3）：
// - 连排级通讯延迟基线 3–10 游戏秒（20 Hz 下 60–200 tick），由场景
//   raw["command_delay"] 数据覆盖（宪法第 12 条：数值以配置承载）。
// - 链路状态机：下达（kIssued）→ 确认接受（kAcknowledged）→ 生效
//   （kEffective）→ 完成（kCompleted）；撤回/取代/拒绝分别落到
//   kWithdrawn/kSuperseded/kRejected。
// - 冲突裁决（FR-041/045）：同一单位同一 tick 到达的多条命令按
//   （优先级降序, 序列号升序）取唯一胜者；后到命令取代同等或更高优先级
//   的已生效命令（新命令取代旧命令），更低优先级被拒绝并产生可见事件。
// - 生效前撤回/修改（FR-045）：WITHDRAW_COMMAND/MODIFY_COMMAND 本身计入
//   通讯延迟，但到达时间封顶为目标命令的到达时间（withdraw_catches_up，
//   可配置），保证撤回/修改在目标命令生效前到达，与"（优先级,序列号）裁决"
//   统一。
// - 批量命令（target.kind == "units"）按每单位独立校验：满足条件的单位
//   接受、不满足的拒绝并产生 BATCH_PARTIAL_ACCEPT 可见事件（FR-045）。
// - 本类型不读取现实时钟，所有时间均为游戏 tick（宪法第 16 条）；
//   延迟抽样使用统一 RNG（宪法第 7 条）。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/clock.h"
#include "wfs/sim/rng.h"

namespace wfs::sim {

struct SimState;  // 内部运行时状态（sim_state.h）；仅实现层需要完整定义。

// 连排级通讯延迟配置（FR-030；默认 3–10s，场景 raw["command_delay"] 可覆盖）。
struct CommandDelayConfig {
    double min_seconds = 3.0;
    double max_seconds = 10.0;
    // 撤回/修改命令到达时间 = min(自身延迟到达, 目标命令到达)：把"生效前
    // 撤回/修改窗口"（FR-045）实现为确定性可测试规则，可配置关闭。
    bool withdraw_catches_up = true;

    std::uint64_t min_ticks(std::uint32_t tick_hz) const;
    std::uint64_t max_ticks(std::uint32_t tick_hz) const;

    // 从场景 raw JSON 读取（缺省回退默认基线；非法值显式抛错，宪法 17）。
    static CommandDelayConfig FromScenario(const nlohmann::json& raw);
};

// 命令链路状态（data-model.md §11 状态机的命令侧投影）。
enum class CommandState : std::uint8_t {
    kIssued = 0,        // 已下达，通讯延迟中。
    kAcknowledged = 1,  // 接收方确认接受（FR-046）。
    kEffective = 2,     // 已生效（仲裁胜出，任务开始执行）。
    kCompleted = 3,     // 任务完成/超时终止。
    kWithdrawn = 4,     // 被撤回（生效前或生效后）。
    kSuperseded = 5,    // 被更高优先级/更新命令取代。
    kRejected = 6,      // 被拒绝（同优先级先到者生效 / 单兵条件不满足）。
};

std::string_view to_string(CommandState state) noexcept;
CommandState command_state_from_string(std::string_view name);

// 链路内单条命令（批量命令展开为父命令 + 每单位子命令）。
struct ChainCommand {
    std::string command_id;  // "cmd-<seq>"（确定性）。
    std::uint64_t seq = 0U;  // 队列序列号（到达顺序权威）。
    std::string type;        // 任务类型或 WITHDRAW_COMMAND/MODIFY_COMMAND。
    std::string unit_id;     // 目标单位；批量父命令为空。
    std::int64_t priority = 0;
    std::uint64_t issue_tick = 0U;
    std::uint64_t delay_ticks = 0U;
    std::uint64_t arrival_tick = 0U;
    CommandState state = CommandState::kIssued;
    nlohmann::json payload;         // 原始命令 JSON（含 replace_with 等）。
    std::string parent_command_id;  // 批量子命令的父命令。
    std::string target_command_id;  // 撤回/修改指向的命令（可为空）。
    bool batch = false;             // 批量父命令或子命令。

    bool operator==(const ChainCommand&) const = default;
};

void to_json(nlohmann::json& json, const ChainCommand& command);
void from_json(const nlohmann::json& json, ChainCommand& command);

// 命令链路：下达（含延迟抽样）→ 到期处理（确认/仲裁/生效），确定性状态容器。
class CommandChain {
   public:
    struct IssueResult {
        bool accepted = false;
        std::string command_id;  // 父命令 id（批量也返回父命令 id）。
        std::uint64_t seq = 0U;
        std::uint64_t delay_ticks = 0U;
        std::uint64_t arrival_tick = 0U;
        std::string error;  // 拒绝原因（空 = 接受）。
    };

    // 下达命令：登记链路、按 RNG 抽延迟、对撤回/修改命令计算封顶到达时间。
    // command 已通过 T014 双重校验；本函数不做重复校验（校验在注入通道）。
    // tick_hz 用于把配置的秒级延迟换算为 tick 数。
    IssueResult Issue(const nlohmann::json& command, std::uint64_t seq, GameTick issue_tick, std::uint32_t tick_hz,
                      const CommandDelayConfig& config, Rng& rng);

    // 每 tick 处理到期命令：确认接受 → （优先级,序列号）裁决 → 生效；
    // 同时检查生效任务时限。事件与单位任务状态写入 SimState。
    void ProcessDue(SimState& state);

    // 任务完成后由机动/任务系统回调（MISSION_COMPLETED）。
    void MarkCompleted(const std::string& command_id);
    // 任务超时/失败处置入口（当前组仅登记事件，完整判定为 T034）。
    static void MarkTimedOut(SimState& state, const std::string& command_id);

    const ChainCommand* Find(const std::string& command_id) const;
    // 可变查找（T034 超时处置等需要修改命令状态；未找到返回 nullptr）。
    ChainCommand* FindMutable(const std::string& command_id);
    std::vector<ChainCommand> CommandsInIssueOrder() const { return commands_; }
    std::size_t size() const noexcept { return commands_.size(); }
    bool empty() const noexcept { return commands_.empty(); }

    void Clear() noexcept { commands_.clear(); }

   private:
    friend void from_json(const nlohmann::json& json, CommandChain& chain);

    std::vector<ChainCommand> commands_;  // 下达顺序（= seq 升序），确定性。
};

void to_json(nlohmann::json& json, const CommandChain& chain);
void from_json(const nlohmann::json& json, CommandChain& chain);

}  // namespace wfs::sim
