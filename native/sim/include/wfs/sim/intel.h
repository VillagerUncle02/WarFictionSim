// sim/include/wfs/sim/intel.h
//
// T033：迷雾/情报识别系统公开接口。
//
// 设计契约（FR-033/034/035；data-model.md §12；SC-006）：
// - 可视距离与观察能力：观察评分 = f(观察方能力、目标隐蔽、距离、环境、
//   烟幕、目标尺寸与移动状态)；评分低于 T1 阈值视为不可见。
// - 识别分档 T1–T3：数量为最低档（T1），种类/构成为更高档（T2），观察能力
//   足够高时同时全部解锁（T3）；档位阈值全部数据驱动。
// - 记忆保留：已识别信息在完全脱离视野后按 memory_ticks 保留档位，超时
//   丢失（FR-034 待办量化项的确定性基线）。
// - 最后动向：连续观察记录归一化运动方向（最后目视动向，FR-035/SC-006）。
// - 情报来源标注与过期：直属发现（direct）标注来源单位/节点，按
//   source_expiry_ticks 过期为 expired；来源过期不删除记忆。
// - 全部记录进确定性状态（sim_state.h intel_records，std::map 键序确定），
//   随存档/快照序列化（宪法第 13 条）。

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace wfs::sim {

struct SimState;          // 内部运行时状态（sim_state.h）。
struct RuntimeUnitState;  // 内部运行时单位（sim_state.h）。

// 识别分档（FR-034：T1 数量 / T2 种类构成 / T3 全识别）。
enum class RecognitionTier : std::uint8_t {
    kNone = 0,
    kT1 = 1,
    kT2 = 2,
    kT3 = 3,
};

std::string_view to_string(RecognitionTier tier) noexcept;
RecognitionTier recognition_tier_from_string(std::string_view name);
void to_json(nlohmann::json& json, RecognitionTier tier);
void from_json(const nlohmann::json& json, RecognitionTier& tier);

// 情报配置（FR-033/034；场景 raw["intel"] 可覆盖，宪法第 12 条）。
struct IntelConfig {
    double base_visibility_range_km = 2.0;       // 基线可视距离。
    double observer_ability_scale = 1.5;         // 观察能力对有效距离的放大系数。
    double t1_threshold = 0.25;                  // T1（数量）评分阈值。
    double t2_threshold = 0.5;                   // T2（种类/构成）评分阈值。
    double t3_threshold = 0.8;                   // T3（全识别）评分阈值。
    double concealment_penalty = 1.0;            // 目标隐蔽对观察评分的衰减。
    double moving_concealment_reduction = 0.15;  // 移动时隐蔽略降（FR-033）。
    double vehicle_size_bonus = 0.15;            // 载具体积大：最低档可识别。
    double smoke_obscuration = 0.9;              // 烟幕遮蔽（FR-023）。
    double degraded_observation_factor = 0.5;    // 观瞄降级/压制观察能力衰减（M5）。
    std::uint64_t memory_ticks = 3600U;          // 脱离识别保留记忆时长。
    std::uint64_t source_expiry_ticks = 2400U;   // 情报来源标注过期时长。

    static IntelConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept {
        return base_visibility_range_km > 0.0 && t1_threshold > 0.0 && t2_threshold >= t1_threshold &&
               t3_threshold >= t2_threshold && t3_threshold <= 1.0 && degraded_observation_factor >= 0.0 &&
               degraded_observation_factor <= 1.0 && memory_ticks > 0U && source_expiry_ticks > 0U;
    }
};

// 单次观察输入（纯确定性；调用方提供地形/烟幕采样结果）。
struct ObservationInput {
    double observer_x = 0.0;
    double observer_y = 0.0;
    double observer_ability = 0.5;  // [0,1]。
    double target_x = 0.0;
    double target_y = 0.0;
    double target_concealment = 0.0;  // [0,1]（地形/工事隐蔽）。
    bool target_is_vehicle = false;
    bool target_moving = false;
    double environment_visibility_multiplier = 1.0;  // 天气/光照（FR-022）。
    bool target_in_smoke = false;
};

struct ObservationResult {
    bool visible = false;
    double distance_km = 0.0;
    double effective_range_km = 0.0;
    double score = 0.0;
};

// 确定性观察结算（不读现实时钟；固定输入固定输出）。
ObservationResult resolve_observation(const ObservationInput& input, const IntelConfig& config);

// 评分 → 识别分档（阈值数据驱动）。
RecognitionTier recognition_tier(double score, const IntelConfig& config);

// 情报来源（data-model.md §12：直属发现/同步/转发；标注可过期）。
struct IntelSource {
    std::string kind;  // "direct" | "sync" | "relay" | "expired"。
    std::string unit_id;
    std::string node_id;
    std::uint64_t reported_tick = 0U;
    std::string level;  // relay 时只标来源层级（"platoon|company|battalion|brigade"，FR-031）。

    bool operator==(const IntelSource&) const = default;
};

// 单条情报记录（按观察方节点 + 目标单位唯一）。
struct IntelRecord {
    std::string observer_node_id;
    std::string target_unit_id;
    RecognitionTier tier = RecognitionTier::kNone;
    std::uint64_t last_seen_tick = 0U;
    std::uint64_t memory_until_tick = 0U;    // 记忆保留截止。
    std::uint64_t source_expires_tick = 0U;  // 来源标注过期截止。
    IntelSource source;
    double last_known_x = 0.0;
    double last_known_y = 0.0;
    double last_motion_dx = 0.0;  // 最后动向（归一化方向）。
    double last_motion_dy = 0.0;
    // T033 登记项（US1 UI 第 2 轮审查 F5）：识别档位核心字段输出。
    // observed_count = 目标人员/乘员规模（最低档数量）；type_name = 目标
    // 类型/型号标识；composition = 目标装备构成（种类/构成档）。
    std::uint64_t observed_count = 0U;
    std::string type_name;
    std::string composition;

    bool operator==(const IntelRecord&) const = default;
};

void to_json(nlohmann::json& json, const IntelSource& source);
void from_json(const nlohmann::json& json, IntelSource& source);
void to_json(nlohmann::json& json, const IntelRecord& record);
void from_json(const nlohmann::json& json, IntelRecord& record);

// 情报记录键：observer_node_id + ":" + target_unit_id（std::map 键序确定）。
std::string intel_record_key(std::string_view observer_node_id, std::string_view target_unit_id);

// 记忆保留：tick < memory_until_tick 返回记录档位，否则 kNone（FR-034）。
RecognitionTier effective_tier(const IntelRecord& record, std::uint64_t tick, const IntelConfig& config);

const IntelRecord* find_intel(const SimState& state, std::string_view observer_node_id,
                              std::string_view target_unit_id);
IntelRecord* find_intel_mutable(SimState& state, std::string_view observer_node_id, std::string_view target_unit_id);

// 外部情报登记（侦察任务等）：合并记录，档位取 max，时间/来源取新值。
void register_intel(SimState& state, const IntelRecord& record);

// 观察一对单位并更新情报记录（返回是否可见）；侦察任务复用。
bool observe_pair(SimState& state, const RuntimeUnitState& observer, const RuntimeUnitState& target);

// 推进一 tick：全量观察 + 记忆/来源过期清理（固定遍历顺序）。
void step_intel(SimState& state);

}  // namespace wfs::sim
