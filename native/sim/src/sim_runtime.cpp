// sim/src/sim_runtime.cpp
//
// T019/T020/T029–T031：模拟推进与玩家命令注入实现。
//
// 实现策略：
// - step_sim_state 与 c_api.cpp 原 wfs_sim_step 语义一致（时钟推进 + 队列
//   补发弹出），并新增 COMMAND_PROCESSED 事件，使无头 JSONL 输出可观察
//   命令链路（宪法 15/17）；随后固定顺序执行 T029 命令链路（确认/仲裁/
//   生效）、T030 机动、T031 战斗结算，保证同输入同输出。
// - inject_player_command 与原 InjectCommand 语义一致（T014 → T011 入队），
//   但经 T029 命令链路登记并抽取连排级 3–10s 通讯延迟：队列仍按 (tick+1,
//   seq) 传输，实际生效 tick 由链路控制；新增 COMMAND_QUEUED/COMMAND_ISSUED
//   事件；校验失败不改变队列（宪法 17）。

#include "sim_runtime.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "wfs/sim/combat.h"
#include "wfs/sim/command_validation.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/model/combat.h"
#include "wfs/sim/movement.h"
#include "wfs/sim/queue.h"

namespace wfs::sim {

namespace {

constexpr std::uint64_t kDefaultAmmoRounds = 100U;  // 基线弹药基数。
constexpr double kBaselineTraining = 0.5;           // 基线受训程度。
constexpr double kBaselineService = 0.5;            // 基线服役时长。
constexpr double kBaselineCombat = 0.4;             // 基线战斗经验。
constexpr double kFullCoverThreshold = 0.5;         // 掩蔽分档阈值（FR-022）。
constexpr double kPartialCoverThreshold = 0.2;

// 从场景文件向上查找仓库 data/ 根（data/units/squads.json + terrain 存在）。
// 相对路径先解析为绝对路径：std::filesystem 对相对路径的 parent_path 不会
// 隐式结合当前工作目录，直接上溯会得到空目录导致数据目录查找失败。
std::filesystem::path FindDataRoot(const std::filesystem::path& scenario_path) {
    std::filesystem::path directory = std::filesystem::absolute(scenario_path).parent_path();
    while (!directory.empty()) {
        std::filesystem::path data_dir = directory / "data";
        if (std::filesystem::exists(data_dir / "units" / "squads.json") &&
            std::filesystem::exists(data_dir / "terrain" / "terrain.json")) {
            return data_dir;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }
    return {};
}

// 从数据目录加载运行期模型库（地形/弹药），失败时保持空库。
void LoadRuntimeLibraries(SimState& state, const std::filesystem::path& data_root) {
    const DataLibraryLoadResult library = load_data_library(data_root);
    if (!library.ok()) {
        return;  // 数据校验失败由加载器在场景层报告；此处不重复失败。
    }
    state.terrain_library.clear();
    state.ammo_library.clear();
    for (const DataEntry& entry : library.library.terrain.entries) {
        state.terrain_library.push_back(entry.raw.get<model::TerrainElement>());
    }
    for (const DataEntry& entry : library.library.ammo.entries) {
        state.ammo_library.push_back(entry.raw.get<model::Ammo>());
    }
}

const DataEntry* FindEntry(const DataCatalog& catalog, const std::string& entry_id) {
    for (const DataEntry& entry : catalog.entries) {
        if (entry.id == entry_id) {
            return &entry;
        }
    }
    return nullptr;
}

// 初始化单个场景单位的运行期状态：武器/弹药/士兵来自编制数据目录。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
RuntimeUnitState MakeRuntimeUnit(const ScenarioUnit& unit, const DataLibraryLoadResult& library,
                                 const std::vector<model::TerrainElement>& terrain_library,
                                 const std::vector<TerrainCell>& terrain_cells) {
    RuntimeUnitState runtime;
    runtime.id = unit.id;
    runtime.type = unit.type;
    runtime.node_id = unit.node_id;
    runtime.x = unit.x;
    runtime.y = unit.y;
    runtime.formation = model::Formation::kMarch;
    runtime.amphibious = true;  // 基线：步兵无重装备可泅渡（FR-022）。

    const DataEntry* squad = FindEntry(library.library.squads, unit.type);
    if (squad != nullptr) {
        const nlohmann::json& raw = squad->raw;
        for (const std::string& weapon_id : raw.value("weapons", std::vector<std::string>{})) {
            if (const DataEntry* weapon = FindEntry(library.library.weapons, weapon_id)) {
                runtime.weapons.push_back(weapon->raw.get<model::Weapon>());
            }
        }
        // 单兵弹药以场景单位声明为准（批量部分接受依赖单位级差异，FR-045），
        // 编制模板声明仅作回退。
        const std::vector<std::string> template_ammo = raw.value("ammo", std::vector<std::string>{});
        const std::vector<std::string>& unit_ammo = unit.ammo.empty() ? template_ammo : unit.ammo;
        for (const std::string& ammo_id : unit_ammo) {
            runtime.ammo[ammo_id] = kDefaultAmmoRounds;  // 基线弹药基数（数据可扩展）。
        }
        const std::uint32_t soldier_count = raw.value("soldier_count", 0U);
        for (std::uint32_t i = 0U; i < soldier_count; ++i) {
            model::Soldier soldier;
            soldier.id = unit.id + "-s" + std::to_string(i);
            soldier.name = "S" + std::to_string(i);
            soldier.weapon_id = runtime.weapons.empty() ? "" : runtime.weapons[i % runtime.weapons.size()].id;
            soldier.experience = model::Experience{kBaselineTraining, kBaselineService, kBaselineCombat};  // 基线经验。
            soldier.protection = model::Protection{true, model::ArmorClass::kLight};                       // 基线防护。
            if (raw.contains("protection") && raw["protection"].is_object()) {
                soldier.protection = raw["protection"].get<model::Protection>();
            }
            runtime.soldiers.push_back(std::move(soldier));
        }
    }

    const TerrainSample terrain = terrain_sample_at(terrain_library, terrain_cells, unit.x, unit.y);
    if (terrain.cover >= kFullCoverThreshold) {
        runtime.cover = model::CoverState::kFull;
    } else if (terrain.cover >= kPartialCoverThreshold) {
        runtime.cover = model::CoverState::kPartial;
    } else {
        runtime.cover = model::CoverState::kNone;
    }
    return runtime;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace

void initialize_runtime_state(SimState& state) {
    state.command_delay_config = CommandDelayConfig::FromScenario(state.scenario.raw);
    state.movement_config = MovementConfig::FromScenario(state.scenario.raw);
    state.combat_config = CombatConfig::FromScenario(state.scenario.raw);
    state.terrain_cells = terrain_cells_from_scenario(state.scenario.raw);

    DataLibraryLoadResult library;
    const std::filesystem::path data_root = FindDataRoot(state.scenario_path);
    if (!data_root.empty()) {
        library = load_data_library(data_root);
        if (library.ok()) {
            LoadRuntimeLibraries(state, data_root);
        }
    }

    state.units.clear();
    for (const ScenarioUnit& unit : state.scenario.units) {
        state.units.push_back(MakeRuntimeUnit(unit, library, state.terrain_library, state.terrain_cells));
    }
}

void step_sim_state(SimState& state) {
    state.clock.advance(1U);
    QueuedEvent event;
    while (state.queue.try_pop(state.clock.tick(), event)) {
        ++state.processed_events;
        state.event_log.append(
            state.clock.tick(), EventCategory::kCommand, EventSeverity::kInfo,
            "COMMAND_PROCESSED seq=" + std::to_string(event.seq) + " tick=" + std::to_string(state.clock.tick()));
    }
    state.command_chain.ProcessDue(state);
    step_movement(state);
    step_combat(state);
}

PlayerCommandResult inject_player_command(SimState& state, const std::string& command_json,
                                          const std::filesystem::path& schema_path) {
    PlayerCommandResult result;
    const CommandValidationContext context = make_validation_context(state.scenario);
    const CommandValidationResult validation = validate_command(command_json, context, schema_path);
    result.errors = validation.errors;
    if (!validation.ok()) {
        return result;
    }
    result.arrival_tick = state.clock.tick();
    result.arrival_seq = state.queue.next_seq();
    nlohmann::json command;
    try {
        command = nlohmann::json::parse(command_json);
    } catch (const nlohmann::json::parse_error&) {
        result.errors.push_back(ValidationError{"INVALID_JSON", "命令不是合法 JSON"});
        return result;
    }
    const CommandChain::IssueResult issued = state.command_chain.Issue(
        command, result.arrival_seq, result.arrival_tick, state.clock.tick_hz(), state.command_delay_config, state.rng);
    if (!issued.accepted) {
        result.errors.push_back(ValidationError{"CHAIN_REJECTED", issued.error});
        return result;
    }
    // 队列作为传输层：到达信号为 issue_tick + 1；真正生效时间由链路控制。
    state.queue.enqueue(result.arrival_tick + 1U, result.arrival_seq, command_json);
    result.accepted = true;
    state.event_log.append(
        result.arrival_tick, EventCategory::kCommand, EventSeverity::kInfo,
        "COMMAND_QUEUED seq=" + std::to_string(result.arrival_seq) + " tick=" + std::to_string(result.arrival_tick));
    std::string unit_text;
    const nlohmann::json& target = command.at("target");
    if (target.at("kind").get<std::string>() == "units") {
        for (const std::string& ref : target.at("refs").get<std::vector<std::string>>()) {
            if (!unit_text.empty()) {
                unit_text += ",";
            }
            unit_text += ref;
        }
    } else {
        unit_text = target.at("ref").get<std::string>();
    }
    state.event_log.append(result.arrival_tick, EventCategory::kCommand, EventSeverity::kInfo,
                           "COMMAND_ISSUED command=" + issued.command_id + " unit=" + unit_text + " type=" +
                               command.at("type").get<std::string>() + " seq=" + std::to_string(result.arrival_seq) +
                               " issue_tick=" + std::to_string(result.arrival_tick) +
                               " delay_ticks=" + std::to_string(issued.delay_ticks) +
                               " arrival_tick=" + std::to_string(issued.arrival_tick));
    return result;
}

}  // namespace wfs::sim
