// sim/src/sim_state.h
//
// 内部共享：模拟核心可变状态容器（T016/T017 快照与存档模块的单一事实来源）。
//
// 设计说明：C ABI 句柄（c_api.cpp）继承 SimState 作为其全部内容物，快照/
// 存档模块只依赖本结构，不依赖 C ABI 边界类型，从而保持依赖方向
// （边界层 → 核心模块，宪法第 14 条）。threads 与 scenario_path 是运行期
// 配置而非游戏状态：threads 不参与状态哈希（宪法第 7 条），scenario_path
// 仅用于 Schema 路径解析。

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "wfs/sim/ai/decision_log.h"
#include "wfs/sim/attach.h"
#include "wfs/sim/clock.h"
#include "wfs/sim/combat.h"
#include "wfs/sim/command_org.h"
#include "wfs/sim/command_chain.h"
#include "wfs/sim/contact.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/faction.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/intel_sync.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/mission_exec.h"
#include "wfs/sim/model/combat.h"
#include "wfs/sim/model/mission.h"
#include "wfs/sim/model/terrain.h"
#include "wfs/sim/movement.h"
#include "wfs/sim/outcome.h"
#include "wfs/sim/queue.h"
#include "wfs/sim/recon_tasks.h"
#include "wfs/sim/rng.h"
#include "wfs/sim/support.h"
#include "wfs/sim/summary.h"
#include "wfs/sim/tactical.h"

namespace wfs::sim {

// T029–T031：场景单位在模拟运行期的可变状态（位置/队形/任务/压制/失联/
// 载具损伤/弹药），是战斗与机动系统的结算对象；随存档序列化（宪法 13）。
struct RuntimeUnitState {
    // 数值字段前置并按 8 字节对齐分组（clang-analyzer Padding 检查）：
    // double → uint64 → int64 → 枚举 → 布尔 → 字符串/容器。
    double x = 0.0;  // km。
    double y = 0.0;
    double suppression = 0.0;
    double last_known_x = 0.0;
    double last_known_y = 0.0;
    double last_known_suppression = 0.0;
    double vehicle_damage = 0.0;
    double vehicle_hp = 0.0;
    double target_x = 0.0;
    double target_y = 0.0;
    std::uint64_t contact_ticks_remaining = 0U;
    std::uint64_t mission_deadline_ticks = 0U;
    std::uint64_t formation_switch_remaining = 0U;
    std::uint64_t last_fire_tick = 0U;
    std::uint64_t fire_cooldown_ticks = 60U;  // 默认 3s（数据可覆盖）。
    std::uint64_t recon_progress_ticks = 0U;  // 侦察阶段/进度（T035）。
    std::uint64_t recon_hold_ticks = 0U;      // 侦察潜伏/观察累计（T034/T035）。
    std::uint32_t recon_shots_fired = 0U;     // 火力侦察已射击轮数（T035）。
    std::int64_t mission_priority = 0;
    model::Formation formation = model::Formation::kMarch;
    model::Formation last_known_formation = model::Formation::kMarch;
    model::CoverState cover = model::CoverState::kNone;
    model::AmmoPolicy ammo_policy = model::AmmoPolicy::kAuto;
    model::Formation requested_formation = model::Formation::kMarch;
    model::ModuleStatus last_known_modules;
    bool out_of_contact = false;
    bool destroyed = false;
    bool is_vehicle = false;
    bool amphibious = false;  // 人员泅渡/载具浮渡（FR-022）。
    bool mission_active = false;
    bool moving = false;
    bool stuck = false;
    bool has_last_known = false;        // 是否已捕获最后已知状态（T032）。
    bool last_known_destroyed = false;  // 最后已知状态中的摧毁标志（T032）。
    bool retreating = false;            // 失败后处置/侦察阶段撤退中（T034/T035）。
    bool mission_loops = true;          // 持续任务循环开关（FR-044）。
    bool recon_detected = false;        // 侦察任务已被发现（T035）。
    std::string id;
    std::string type;
    std::string node_id;
    std::string side;  // 阵营（数据驱动；缺省按 node_id 派生，M3）。
    model::ArmorProfile vehicle_armor;
    model::ModuleStatus vehicle_modules;
    std::vector<model::Weapon> weapons;
    std::map<std::string, std::uint64_t> ammo;  // ammo_id -> 余弹。
    std::vector<model::Soldier> soldiers;       // 班组/乘员/载员运行期副本。
    std::size_t crew_count = 0U;                // 载具乘员数（soldiers 前 N 个）；
                                                // 班组 = soldiers.size()，弃车按此拆分（FR-062）。

    // 任务（命令链生效后写入；完整判定为 T034，本组覆盖 MOVE 闭环）。
    std::string mission_command_id;
    std::string mission_type;
    std::string mission_condition;
    nlohmann::json mission_params = nlohmann::json::object();
    std::string ammo_override;
    std::string failure_action = "report";  // 失败后处置（FR-044）。
    std::string failure_target;             // withdraw_to 撤退目标 "x,y"。

    // 战斗节流（T031 自动接敌）。
    std::string last_exhausted_weapon;  // 防事件刷屏：弹药耗尽只报一次/武器。

    bool operator==(const RuntimeUnitState&) const = default;
};

void to_json(nlohmann::json& json, const RuntimeUnitState& unit);
void from_json(const nlohmann::json& json, RuntimeUnitState& unit);

struct SimState {
    Scenario scenario;        // 场景数据（T013 加载结果，含 raw JSON）。
    GameClock clock;          // 离散 tick 游戏时钟（T010）。
    Rng rng;                  // 统一确定性 RNG（T009）。
    EventQueue queue;         // 事件/命令确定性队列（T011）。
    EventLog event_log;       // 事件日志（T015）。
    std::uint64_t seed = 0U;  // 创建句柄时的显式种子（运行身份标识）。
    int threads = 1;          // 并行度配置（只影响性能，不进哈希/存档状态）。
    std::uint64_t processed_events = 0U;
    DecisionLog decision_log;                // T020：AI 决策点记录（宪法 10）。
    std::uint64_t ai_decision_counter = 0U;  // T020：决策编号单调游标（接受/拒绝共用）。
    std::filesystem::path scenario_path;

    // T029–T031：命令链路、运行期单位、烟幕与配置（配置派生自场景，
    // 不参与状态哈希；单位/链路/烟幕随存档序列化）。
    CommandChain command_chain;
    std::vector<RuntimeUnitState> units;
    std::vector<SmokeArea> smoke_areas;
    std::uint64_t next_smoke_id = 0U;
    CommandDelayConfig command_delay_config;
    MovementConfig movement_config;
    CombatConfig combat_config;
    ContactConfig contact_config;
    IntelConfig intel_config;
    MissionExecConfig mission_config;
    ReconConfig recon_config;
    OutcomeConfig outcome_config;
    // T047–T050：支援请求/配属/战术编成状态（确定性，随存档序列化）。
    SupportConfig support_config;
    SupportChain support_chain;
    AttachRegistry attach_registry;
    TacticalRegistry tactical_registry;
    FactionTemplate support_faction;                 // 生效派系模板（数据派生）。
    std::string support_pool_echelon = "battalion";  // 连排级分数取营编制池。
    std::uint64_t support_score_remaining = 0U;      // 连排级剩余分数（FR-008）。
    bool support_configured = false;                 // 场景声明支援配置后置 true。
    // T033/T036：情报记录与胜负/目标进度（确定性状态，随存档序列化）。
    std::map<std::string, IntelRecord> intel_records;
    std::vector<ObjectiveRuntimeState> objective_states;
    OutcomeState outcome;
    std::vector<model::TerrainElement> terrain_library;  // 运行期派生，不进哈希。
    std::vector<TerrainCell> terrain_cells;              // 运行期派生，不进哈希。
    std::vector<model::Ammo> ammo_library;               // 运行期派生，不进哈希。
    // T057/T058：指挥组织与层级化情报同步状态（确定性，随存档序列化；
    // 未配置 command_org 的场景保持省略字段，旧存档兼容）。配置派生自场景，
    // 不进哈希；同步时刻表是确定性状态、进哈希。
    CommandOrgState command_org;
    IntelSyncConfig intel_sync_config;
    IntelSyncState intel_sync_state;
    // T059：摘要上报状态与任务结果统计（确定性，随存档序列化）。
    SummaryConfig summary_config;
    SummaryRegistry summaries;
    std::map<std::string, MissionOutcomeCounts> mission_outcomes;
};

// 玩家阵营：按 player_node_id 对应单位的 side 派生；缺省回退 node_id
// （与旧场景 node_id 分组兼容，M3）。
inline std::string friendly_side(const SimState& state) {
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.node_id == state.scenario.player_node_id) {
            return unit.side;
        }
    }
    return state.scenario.player_node_id;
}

inline void to_json(nlohmann::json& json, const RuntimeUnitState& unit) {
    json = nlohmann::json{{"id", unit.id},
                          {"type", unit.type},
                          {"node_id", unit.node_id},
                          {"side", unit.side},
                          {"x", unit.x},
                          {"y", unit.y},
                          {"formation", unit.formation},
                          {"cover", unit.cover},
                          {"suppression", unit.suppression},
                          {"last_known_x", unit.last_known_x},
                          {"last_known_y", unit.last_known_y},
                          {"last_known_suppression", unit.last_known_suppression},
                          {"last_known_formation", unit.last_known_formation},
                          {"last_known_modules", unit.last_known_modules},
                          {"has_last_known", unit.has_last_known},
                          {"last_known_destroyed", unit.last_known_destroyed},
                          {"out_of_contact", unit.out_of_contact},
                          {"contact_ticks_remaining", unit.contact_ticks_remaining},
                          {"destroyed", unit.destroyed},
                          {"is_vehicle", unit.is_vehicle},
                          {"amphibious", unit.amphibious},
                          {"vehicle_damage", unit.vehicle_damage},
                          {"vehicle_hp", unit.vehicle_hp},
                          {"vehicle_armor", unit.vehicle_armor},
                          {"vehicle_modules", unit.vehicle_modules},
                          {"weapons", unit.weapons},
                          {"ammo", unit.ammo},
                          {"soldiers", unit.soldiers},
                          {"crew_count", unit.crew_count},
                          {"mission_active", unit.mission_active},
                          {"mission_command_id", unit.mission_command_id},
                          {"mission_type", unit.mission_type},
                          {"mission_priority", unit.mission_priority},
                          {"mission_deadline_ticks", unit.mission_deadline_ticks},
                          {"mission_condition", unit.mission_condition},
                          {"mission_params", unit.mission_params},
                          {"ammo_policy", unit.ammo_policy},
                          {"ammo_override", unit.ammo_override},
                          {"failure_action", unit.failure_action},
                          {"failure_target", unit.failure_target},
                          {"mission_loops", unit.mission_loops},
                          {"recon_progress_ticks", unit.recon_progress_ticks},
                          {"recon_hold_ticks", unit.recon_hold_ticks},
                          {"recon_shots_fired", unit.recon_shots_fired},
                          {"recon_detected", unit.recon_detected},
                          {"retreating", unit.retreating},
                          {"moving", unit.moving},
                          {"target_x", unit.target_x},
                          {"target_y", unit.target_y},
                          {"stuck", unit.stuck},
                          {"formation_switch_remaining", unit.formation_switch_remaining},
                          {"requested_formation", unit.requested_formation},
                          {"last_fire_tick", unit.last_fire_tick},
                          {"fire_cooldown_ticks", unit.fire_cooldown_ticks},
                          {"last_exhausted_weapon", unit.last_exhausted_weapon}};
}

inline void from_json(const nlohmann::json& json, RuntimeUnitState& unit) {
    unit.id = json.at("id").get<std::string>();
    unit.type = json.at("type").get<std::string>();
    unit.node_id = json.at("node_id").get<std::string>();
    // F1：旧存档（无 side 字段）加载后必须按 node_id 兜底阵营，否则 side==""
    // 会使敌我判定恒同阵营（战斗/侦察/任务/胜负静默失效）；node_id 已先行解析。
    unit.side = json.value("side", unit.node_id);
    unit.x = json.at("x").get<double>();
    unit.y = json.at("y").get<double>();
    unit.formation = json.at("formation").get<model::Formation>();
    unit.cover = json.at("cover").get<model::CoverState>();
    unit.suppression = json.at("suppression").get<double>();
    // T032–T036 新增字段：旧存档缺失时按默认值恢复（非破坏性演进）。
    unit.last_known_x = json.value("last_known_x", 0.0);
    unit.last_known_y = json.value("last_known_y", 0.0);
    unit.last_known_suppression = json.value("last_known_suppression", 0.0);
    unit.last_known_formation = json.value("last_known_formation", model::Formation::kMarch);
    unit.last_known_modules = json.value("last_known_modules", model::ModuleStatus{});
    unit.has_last_known = json.value("has_last_known", false);
    unit.last_known_destroyed = json.value("last_known_destroyed", false);
    unit.out_of_contact = json.at("out_of_contact").get<bool>();
    unit.contact_ticks_remaining = json.at("contact_ticks_remaining").get<std::uint64_t>();
    unit.destroyed = json.at("destroyed").get<bool>();
    unit.is_vehicle = json.at("is_vehicle").get<bool>();
    unit.amphibious = json.at("amphibious").get<bool>();
    unit.vehicle_damage = json.at("vehicle_damage").get<double>();
    unit.vehicle_hp = json.at("vehicle_hp").get<double>();
    unit.vehicle_armor = json.at("vehicle_armor").get<model::ArmorProfile>();
    unit.vehicle_modules = json.at("vehicle_modules").get<model::ModuleStatus>();
    unit.weapons = json.at("weapons").get<std::vector<model::Weapon>>();
    unit.ammo = json.at("ammo").get<std::map<std::string, std::uint64_t>>();
    unit.soldiers = json.at("soldiers").get<std::vector<model::Soldier>>();
    // 存档兼容（宪法 13）：fc62e4b 生成的 v1 存档（units 无 crew_count 字段）
    // 必须可加载。班组旧存档默认 crew_count = soldiers.size()（全员视为乘员，
    // 与班组语义一致）；载具旧存档（该字段引入前仓库数据中不存在载具运行期
    // 单位）按全员视为乘员保守处理——弃车时全部进入乘员组，不丢失人员。
    unit.crew_count = json.value("crew_count", unit.soldiers.size());
    unit.mission_active = json.at("mission_active").get<bool>();
    unit.mission_command_id = json.at("mission_command_id").get<std::string>();
    unit.mission_type = json.at("mission_type").get<std::string>();
    unit.mission_priority = json.at("mission_priority").get<std::int64_t>();
    unit.mission_deadline_ticks = json.at("mission_deadline_ticks").get<std::uint64_t>();
    unit.mission_condition = json.at("mission_condition").get<std::string>();
    unit.mission_params = json.at("mission_params");
    unit.ammo_policy = json.at("ammo_policy").get<model::AmmoPolicy>();
    unit.ammo_override = json.at("ammo_override").get<std::string>();
    unit.failure_action = json.value("failure_action", std::string("report"));
    unit.failure_target = json.value("failure_target", std::string());
    unit.mission_loops = json.value("mission_loops", true);
    unit.recon_progress_ticks = json.value("recon_progress_ticks", 0U);
    unit.recon_hold_ticks = json.value("recon_hold_ticks", 0U);
    unit.recon_shots_fired = json.value("recon_shots_fired", 0U);
    unit.recon_detected = json.value("recon_detected", false);
    unit.retreating = json.value("retreating", false);
    unit.moving = json.at("moving").get<bool>();
    unit.target_x = json.at("target_x").get<double>();
    unit.target_y = json.at("target_y").get<double>();
    unit.stuck = json.at("stuck").get<bool>();
    unit.formation_switch_remaining = json.at("formation_switch_remaining").get<std::uint64_t>();
    unit.requested_formation = json.at("requested_formation").get<model::Formation>();
    unit.last_fire_tick = json.at("last_fire_tick").get<std::uint64_t>();
    unit.fire_cooldown_ticks = json.at("fire_cooldown_ticks").get<std::uint64_t>();
    unit.last_exhausted_weapon = json.at("last_exhausted_weapon").get<std::string>();
}

}  // namespace wfs::sim
