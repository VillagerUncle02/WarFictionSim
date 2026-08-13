// 文件总览：UI↔核心互操作 —— 快照根 DTO（T043，只读消费）。
//
// 字段镜像 native/sim/src/snapshot.cpp 的 build_snapshot_json 输出键。
// 全部属性 init-only、集合为只读接口：快照一旦解析完成就不可变，
// 防止表现层任何代码把"渲染用数据"误当作"模拟状态"写回（宪法第 14 条）。

namespace WarFictionSim.Ui.Interop;

/// <summary>模拟状态只读快照（表现层数据入口）。</summary>
public sealed class SimulationSnapshot
{
    /// <summary>核心 ABI 版本字符串。</summary>
    public required string AbiVersion { get; init; }

    /// <summary>当前游戏 tick。</summary>
    public required ulong Tick { get; init; }

    /// <summary>模拟已推进的累计微秒数（纯展示元数据，不参与结算）。</summary>
    public required ulong TotalUs { get; init; }

    /// <summary>创建句柄时的显式随机种子。</summary>
    public required ulong Seed { get; init; }

    /// <summary>并行度配置（只影响性能，不影响状态哈希）。</summary>
    public required int Threads { get; init; }

    /// <summary>场景标识。</summary>
    public required string ScenarioId { get; init; }

    /// <summary>场景显示名。</summary>
    public required string ScenarioName { get; init; }

    /// <summary>玩家扮演的指挥节点 id（空串表示场景未指定）。</summary>
    public required string PlayerNodeId { get; init; }

    /// <summary>确定性队列中待处理事件数。</summary>
    public required ulong PendingEvents { get; init; }

    /// <summary>已处理事件总数。</summary>
    public required ulong ProcessedEvents { get; init; }

    /// <summary>事件日志保留摘要（条目正文不进入快照，见 snapshot.cpp）。</summary>
    public required EventLogSummaryState EventLog { get; init; }

    /// <summary>命令链路摘要。</summary>
    public required CommandChainSummaryState CommandChain { get; init; }

    /// <summary>场景全部单位运行期状态（含敌方；是否展示由迷雾层决定）。</summary>
    public required IReadOnlyList<UnitState> Units { get; init; }

    /// <summary>情报记录表，键为 "observer_node_id:target_unit_id"。 </summary>
    public required IReadOnlyDictionary<string, IntelRecordState> IntelRecords { get; init; }

    /// <summary>关键目标运行期进度。</summary>
    public required IReadOnlyList<ObjectiveState> Objectives { get; init; }

    /// <summary>胜负判定状态。</summary>
    public required OutcomeState Outcome { get; init; }
}
