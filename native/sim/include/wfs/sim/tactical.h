// sim/include/wfs/sim/tactical.h
//
// T048：战术分队/火力组模型公开接口。
//
// 设计契约（data-model.md §4；FR-010）：
// - 战术管理独立于行政编制：拆分（班组→火力组）、合并（损失严重班组临时
//   合并）、跨行政编制组建战术分队都不改写行政编制；"解除战术编成、恢复
//   行政编制"是拆分状态下的显式操作（FR-010）。
// - 拆分状态下命令作用域为火力组：整班命令被拒绝（SPLIT_SQUAD），火力组
//   命令放行；恢复行政编制后整班命令恢复（FR-010/045）。
// - 任务结束归建仅归建存活成员，损失/失联成员按伤亡记录留在编制外
//   （FR-010/044）；所有结算为纯函数，无随机数/时钟（宪法第 7 条）。
// - 值类型支持 nlohmann::json 往返序列化（宪法第 13 条）。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim {

// 功能组类型（data-model.md §4：破障组/火力组/突击组/保障组等，可扩展）。
enum class TacticalKind : std::uint8_t {
    kFireTeam = 0,    // 火力组。
    kBreachTeam = 1,  // 破障组。
    kAssaultTeam = 2, // 突击组。
    kSupportTeam = 3, // 保障组。
};

// 战术编成生命周期状态。
enum class TacticalState : std::uint8_t {
    kActive = 0,     // 编成中/任务执行中。
    kDissolved = 1,  // 已解除战术编成（恢复行政编制）。
};

std::string_view to_string(TacticalKind kind) noexcept;
TacticalKind tactical_kind_from_string(std::string_view name);
std::string_view to_string(TacticalState state) noexcept;
TacticalState tactical_state_from_string(std::string_view name);

// 火力组：行政班的临时战术拆分（不影响行政编制，FR-010）。
struct FireTeam {
    std::string id;                // "ft-<squad>-<n>"（确定性命名）。
    std::string admin_squad_id;    // 行政班 id。
    std::vector<std::string> soldier_ids;  // 成员（最小配备单位/士兵 id）。
    TacticalKind kind = TacticalKind::kFireTeam;
    TacticalState state = TacticalState::kActive;

    bool operator==(const FireTeam&) const = default;
};

// 战术分队：按任务跨行政编制临时组合（data-model.md §4）。
struct TacticalTaskForce {
    std::string id;
    std::string task_id;                 // 关联任务（原任务/命令 id）。
    std::vector<std::string> member_unit_ids;  // 成员最小可指挥单位（含火力组）。
    TacticalState state = TacticalState::kActive;

    bool operator==(const TacticalTaskForce&) const = default;
};

// 拆分结果：确定性按 group_size 把成员分配到 team_count 个火力组。
struct SplitResult {
    bool ok = false;
    std::vector<FireTeam> teams;
    std::string error;  // 失败原因（空 = 成功）。
};

// 合并结果：损失严重班组临时合并为一个火力组继续执行任务。
struct MergeResult {
    bool ok = false;
    FireTeam merged;
    std::string error;
};

// 命令作用域裁决（FR-010/045）：拆分状态下整班命令拒绝。
struct CommandScopeResult {
    bool valid = false;
    std::string effective_unit_id;  // 放行时实际可指挥单位（火力组/原单位）。
    std::string error;              // 拒绝原因（SPLIT_SQUAD_COMMAND_NOT_ALLOWED）。
};

// 归建结算（纯函数）：仅存活成员归建，损失/失联成员按伤亡记录不归建。
struct ReturnToParentResult {
    bool ok = false;
    std::vector<std::string> returned_unit_ids;    // 存活归建成员（顺序确定）。
    std::vector<std::string> casualty_unit_ids;    // 损失/失联成员（伤亡记录）。
    std::string error;
};

void to_json(nlohmann::json& json, const FireTeam& team);
void from_json(const nlohmann::json& json, FireTeam& team);
void to_json(nlohmann::json& json, const TacticalTaskForce& force);
void from_json(const nlohmann::json& json, TacticalTaskForce& force);

// 战术编成登记表：火力组/战术分队生命周期与命令作用域裁决。
class TacticalRegistry {
   public:
    // 拆分班组：把成员清单按 team_count 个火力组确定性分配（余数依次补入
    // 前面的组，保证同输入同结果）。成员清单由调用方提供（运行期士兵清单），
    // 注册表不持有行政编制数据；班组已拆分时失败。
    SplitResult SplitSquad(const std::string& squad_id, const std::vector<std::string>& soldier_ids,
                           std::size_t team_count);
    // 合并损失严重班组：多个班组幸存成员合入 merged_id 火力组（临时合并，
    // 不影响行政编制）；成员清单由调用方拼接后传入。
    MergeResult MergeDepletedSquads(const std::string& merged_id, const std::string& admin_squad_id,
                                    const std::vector<std::string>& soldier_ids);
    // 解除战术编成、恢复行政编制：解散该班全部火力组（FR-010）。
    bool DissolveFireTeams(const std::string& squad_id);
    // 组建/解散跨行政编制战术分队。
    bool FormTaskForce(TacticalTaskForce force);
    bool DissolveTaskForce(const std::string& task_force_id);

    // 命令作用域：火力组 id 放行；已拆分班组 id 拒绝；其余按原单位放行。
    CommandScopeResult ResolveCommandScope(const std::string& unit_id) const;

    bool IsSplit(const std::string& squad_id) const;
    const FireTeam* FindFireTeam(const std::string& id) const;
    const TacticalTaskForce* FindTaskForce(const std::string& id) const;
    std::vector<std::string> ActiveFireTeamIds(const std::string& squad_id) const;
    std::vector<FireTeam> FireTeamsInInsertionOrder() const { return fire_teams_; }
    std::vector<TacticalTaskForce> TaskForcesInInsertionOrder() const { return task_forces_; }
    std::size_t size() const noexcept { return fire_teams_.size() + task_forces_.size(); }
    bool empty() const noexcept { return fire_teams_.empty() && task_forces_.empty(); }
    void Clear() noexcept;

   private:
    friend void from_json(const nlohmann::json& json, TacticalRegistry& registry);

    std::vector<FireTeam> fire_teams_;
    std::vector<TacticalTaskForce> task_forces_;
};

void to_json(nlohmann::json& json, const TacticalRegistry& registry);
void from_json(const nlohmann::json& json, TacticalRegistry& registry);

// 归建结算纯函数：active_member_ids 为存活成员集合；输出只含存活成员，
// 不在集合内的成员进入伤亡记录（FR-010：损失/失联不归建）。
ReturnToParentResult compute_return_to_parent(const TacticalTaskForce& force,
                                              const std::vector<std::string>& active_member_ids);

}  // namespace wfs::sim
