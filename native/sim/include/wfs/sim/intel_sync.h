// sim/include/wfs/sim/intel_sync.h
//
// T058：层级化情报同步公开接口。
//
// 设计契约（FR-028/029/031/032；data-model.md §12；宪法第 7 条）：
// - 同步间隔按指挥层级数据驱动：连排级（排/连）5s、营级 15s、旅级 60s
//   （20 Hz 下 100/300/1200 tick），层级越高间隔越长；位置、状态、敌情
//   三类信息共用同一间隔（同步事件一次性携带三类汇总）。
// - 直属可指挥单位实时同步：本节点所属单位的位置/状态即节点本地状态，
//   每个 tick 刷新 last_direct_sync_tick；向上传递仍按层级间隔（FR-028
//   "近乎实时"指连排级对本单位，不改变向上同步节奏）。
// - 层级同步沿指挥树向上合并子节点情报：子节点直属发现（direct）上报为
//   sync（标注具体发现单位），再向上转发为 relay（只标注来源层级，FR-031）；
//   来源标注随新条目更新、不被新情报覆盖的旧条目按 source_expiry 过期。
// - 最后已知状态（FR-029/065）：子节点失联（CP 单位 out_of_contact）时其
//   下属单位冻结为最后已知，同步事件携带 last_known 计数且不合并新情报；
//   恢复后按层级从低到高逐级延迟（下一次本层级同步才重新携带实时值）。
// - 全部状态（last_hierarchy_sync_tick / last_direct_sync_tick）进入
//   SimState，随存档/快照序列化（宪法第 13 条），遍历顺序固定确定。

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "wfs/sim/model/organization.h"

namespace wfs::sim {

struct SimState;  // 内部运行时状态（sim_state.h）。

// 层级同步配置（FR-028；场景 raw["intel_sync"] 可覆盖，宪法第 12 条）。
struct IntelSyncConfig {
    std::uint64_t platoon_sync_ticks = 100U;    // 连排级基线 5s（20 Hz）。
    std::uint64_t company_sync_ticks = 100U;    // 连级与排级同属"连排级"。
    std::uint64_t battalion_sync_ticks = 300U;  // 营级基线 15s。
    std::uint64_t brigade_sync_ticks = 1200U;   // 旅级基线 60s（T076 接入）。
    bool realtime_direct_units = true;          // 直属可指挥单位实时同步。

    static IntelSyncConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept {
        return platoon_sync_ticks > 0U && company_sync_ticks > 0U && battalion_sync_ticks >= company_sync_ticks &&
               brigade_sync_ticks >= battalion_sync_ticks;
    }

    // 节点层级 → 同步间隔（层级越高间隔越长，FR-028）。
    std::uint64_t sync_ticks_for(model::Echelon echelon) const noexcept;
};

// 层级同步状态：每节点上次层级同步/直属实时同步的 tick（确定性，随存档序列化）。
struct IntelSyncState {
    std::map<std::string, std::uint64_t> last_hierarchy_sync_tick;
    std::map<std::string, std::uint64_t> last_direct_sync_tick;

    bool operator==(const IntelSyncState&) const = default;
};

void to_json(nlohmann::json& json, const IntelSyncState& state);
void from_json(const nlohmann::json& json, IntelSyncState& state);

// 推进一 tick 的层级化情报同步：直属实时同步 → 按层级间隔向上合并
// （固定节点插入顺序与情报 map 键序，保证确定性）。
void step_intel_sync(SimState& state);

}  // namespace wfs::sim
