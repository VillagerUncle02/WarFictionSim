// sim/include/wfs/sim/comm.h
//
// T060：通信状态模型公开接口。
//
// 设计契约（FR-077；data-model.md §9–10；宪法第 7/12/17 条）：
// - 通信表现为状态：两条确定性链路——单位↔所属节点（直属指挥）与
//   子节点↔父节点（层级上送）；链路中断时指令延迟到达、情报冻结为最后
//   已知状态，与失联机制衔接。
// - 通信范围由四类输入共同决定：通信装备属性（单位 comm_power，缺省 1.0）、
//   通信保障部队（comm_role=support，失能时范围按 support_disabled_factor
//   缩小，长距离链路中断从而同步延迟）、地形修正（端点所在地形罚值取较严
//   者）与民用通讯设施修正（端点位于设施半径内加增益）；全部数值场景
//   raw["comm"] 数据驱动。节点链路端点优先绑定该节点的通信保障部队（首个
//   comm_role=support 单位），普通单位仅作位置代理、不承载节点链路健康
//   （摧毁普通单位不切断节点链路）。
// - 中断与失联取更严（CHK163）：链路有效 = 通信连通 且 两端单位均未失联/
//   未摧毁；两者独立恢复——通信按位置/保障状态恢复，失联按 contact.cpp
//   规则恢复，互不等待。
// - 确定性：链路按单位列表顺序/节点插入顺序生成，每 tick 固定顺序判定；
//   CommState 随存档序列化（宪法第 13 条），CommConfig/单位档案是场景派生
//   数据、不进哈希。

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim {

struct SimState;          // 内部运行时状态（sim_state.h）。
struct RuntimeUnitState;  // 内部运行期单位（sim_state.h）。

// 单位通信档案（场景派生：unit.comm_power/comm_role，FR-077）。
struct CommUnitProfile {
    double power = 1.0;
    std::string role = "standard";  // "standard" | "support"。

    bool is_support() const noexcept { return role == "support"; }
    bool operator==(const CommUnitProfile&) const = default;
};

// 链路类型：unit = 单位↔所属节点；node = 子节点↔父节点。
enum class CommLinkKind : std::uint8_t {
    kUnit = 0,
    kNode = 1,
};

std::string_view to_string(CommLinkKind kind) noexcept;
CommLinkKind comm_link_kind_from_string(std::string_view name);
void to_json(nlohmann::json& json, CommLinkKind kind);
void from_json(const nlohmann::json& json, CommLinkKind& kind);

// 民用通讯设施修正输入（场景 raw["comm"].civilian_facilities，FR-024/077）。
struct CivilianCommFacility {
    std::string id;
    double x = 0.0;
    double y = 0.0;
    double radius_km = 0.0;

    bool operator==(const CivilianCommFacility&) const = default;
};

// 通信配置（场景 raw["comm"] 可覆盖；command_org 未配置时整体不激活）。
struct CommConfig {
    double base_range_km = 5.0;                        // 通信装备基线范围。
    double power_scale = 1.0;                          // 装备功率对范围的放大系数。
    double support_force_bonus_km = 1.0;               // 通信保障部队增益。
    double support_disabled_factor = 0.4;              // 保障部队失能后的范围系数。
    std::map<std::string, double> terrain_penalty_km;  // terrain_id → 罚值。
    double civilian_facility_bonus_km = 1.0;
    std::vector<CivilianCommFacility> civilian_facilities;
    double min_range_km = 0.5;  // 有效范围下界（保底近距通信）。

    static CommConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept;
};

// 单条链路状态（确定性，随存档序列化）。
struct CommLinkStatus {
    std::string id;  // "link-<n>"（确定性单调）。
    CommLinkKind kind = CommLinkKind::kUnit;
    std::string from_id;
    std::string to_id;
    bool connected = true;  // 纯通信判定（范围/装备/保障/地形/设施）。
    std::string reason;     // "" | OUT_OF_RANGE | SUPPORT_DISABLED | ENDPOINT_DISABLED。
    std::uint64_t outage_start_tick = 0U;
    std::uint64_t restored_tick = 0U;
    double last_known_x = 0.0;  // 断链/失联时冻结的最后已知位置。
    double last_known_y = 0.0;
    std::uint64_t last_known_tick = 0U;

    bool operator==(const CommLinkStatus&) const = default;
};

struct CommState {
    std::vector<CommLinkStatus> links;

    const CommLinkStatus* Find(CommLinkKind kind, const std::string& from_id, const std::string& to_id) const;
    std::size_t outage_count() const noexcept;
};

void to_json(nlohmann::json& json, const CommLinkStatus& link);
void from_json(const nlohmann::json& json, CommLinkStatus& link);
void to_json(nlohmann::json& json, const CommState& state);
void from_json(const nlohmann::json& json, CommState& state);

// 两点通信有效范围（纯确定性）：base_range_km × 装备功率平均 × power_scale
// + 保障增益 × 保障系数 − 地形罚值（端点较严者）+ 民用通讯设施增益；
// 下界 min_range_km。
double effective_comm_range_km(const CommConfig& config, double power_from, double power_to, double support_factor,
                               const std::string& terrain_from, const std::string& terrain_to, double from_x,
                               double from_y, double to_x, double to_y);

// 链路生效判定（FR-077：中断与失联取更严）：通信连通且两端单位均未失联/摧毁。
bool link_effective(const SimState& state, const CommLinkStatus& link);

// 单位↔所属节点链路是否生效；未知单位返回 true（无通信约束时不阻塞）。
bool unit_link_effective(const SimState& state, const std::string& unit_id);
// 子节点↔父节点链路是否生效；无此链路返回 true。
bool node_link_effective(const SimState& state, const std::string& child_node_id, const std::string& parent_node_id);
// 节点通信载体单位：该节点 comm_role=support 的首个单位（单位列表顺序）；
// 无保障部队返回 nullptr（普通单位不承载节点链路，FR-077）。
const RuntimeUnitState* node_carrier_unit(const SimState& state, const std::string& node_id);

// 初始化链路表（单位列表顺序 + 节点插入顺序；场景派生，不进哈希）。
void initialize_comm_state(SimState& state);
// 推进一 tick：判定连通/中断/恢复、冻结最后已知、门控到期命令的到达时间。
void step_comm(SimState& state);

}  // namespace wfs::sim
