// 文件总览：UI↔核心互操作 —— 单位运行期状态 DTO（T043，只读消费）。
//
// 字段镜像 native/sim/src/sim_state.h RuntimeUnitState 的 to_json 输出键。
// 只保留表现层渲染所需子集；未知字段由 System.Text.Json 忽略——因为快照
// 永不写回核心，忽略不代表数据丢失（宪法第 14 条：跨层只传纯数据）。

namespace WarFictionSim.Ui.Interop;

/// <summary>单位运行期状态（快照只读视图）。</summary>
public sealed class UnitState
{
    /// <summary>单位唯一 id。</summary>
    public required string Id { get; init; }

    /// <summary>单位类型（数据目录 id，如 squad-rifle-us）。</summary>
    public required string Type { get; init; }

    /// <summary>所属指挥节点 id。</summary>
    public required string NodeId { get; init; }

    /// <summary>阵营标识（缺省场景按 node_id 兜底）。</summary>
    public required string Side { get; init; }

    /// <summary>世界横坐标（km）。</summary>
    public required double X { get; init; }

    /// <summary>世界纵坐标（km）。</summary>
    public required double Y { get; init; }

    /// <summary>当前队形（march/combat）。</summary>
    public required string Formation { get; init; }

    /// <summary>掩蔽状态（none/forest/…）。</summary>
    public required string Cover { get; init; }

    /// <summary>压制值（0–1）。</summary>
    public required double Suppression { get; init; }

    /// <summary>最后已知横坐标（失联快照，km）。</summary>
    public required double LastKnownX { get; init; }

    /// <summary>最后已知纵坐标（失联快照，km）。</summary>
    public required double LastKnownY { get; init; }

    /// <summary>最后已知压制值。</summary>
    public required double LastKnownSuppression { get; init; }

    /// <summary>最后已知队形。</summary>
    public required string LastKnownFormation { get; init; }

    /// <summary>是否已捕获最后已知状态（失联单位）。</summary>
    public required bool HasLastKnown { get; init; }

    /// <summary>最后已知状态中的摧毁标志。</summary>
    public required bool LastKnownDestroyed { get; init; }

    /// <summary>是否处于失联状态。</summary>
    public required bool OutOfContact { get; init; }

    /// <summary>失联恢复剩余 tick 数。</summary>
    public required ulong ContactTicksRemaining { get; init; }

    /// <summary>是否已被摧毁。</summary>
    public required bool Destroyed { get; init; }

    /// <summary>是否为载具。</summary>
    public required bool IsVehicle { get; init; }

    /// <summary>是否具备两栖能力（人员泅渡/载具浮渡）。</summary>
    public required bool Amphibious { get; init; }

    /// <summary>载具损伤值。</summary>
    public required double VehicleDamage { get; init; }

    /// <summary>载具生命值。</summary>
    public required double VehicleHp { get; init; }

    /// <summary>士兵数（班组全员/乘员+载员）。</summary>
    public required int SoldierCount { get; init; }

    /// <summary>乘员数（载具时有效；班组等于士兵数）。</summary>
    public required int CrewCount { get; init; }

    /// <summary>弹药余量：ammo_id → 余弹数。</summary>
    public required IReadOnlyDictionary<string, ulong> Ammo { get; init; }

    /// <summary>是否有执行中任务。</summary>
    public required bool MissionActive { get; init; }

    /// <summary>执行中任务的命令 id（空串表示无）。</summary>
    public required string MissionCommandId { get; init; }

    /// <summary>执行中任务类型（空串表示无）。</summary>
    public required string MissionType { get; init; }

    /// <summary>执行中任务优先级。</summary>
    public required long MissionPriority { get; init; }

    /// <summary>执行中任务时限（游戏 tick）。</summary>
    public required ulong MissionDeadlineTicks { get; init; }

    /// <summary>执行中任务完成条件。</summary>
    public required string MissionCondition { get; init; }

    /// <summary>持续任务循环开关。</summary>
    public required bool MissionLoops { get; init; }

    /// <summary>弹药策略（auto/specific）。</summary>
    public required string AmmoPolicy { get; init; }

    /// <summary>任务级弹药覆盖 id（空串表示自动选弹）。</summary>
    public required string AmmoOverride { get; init; }

    /// <summary>失败后处置（withdraw_to/hold/report）。</summary>
    public required string FailureAction { get; init; }

    /// <summary>withdraw_to 的撤退目标 "x,y"（空串表示无）。</summary>
    public required string FailureTarget { get; init; }

    /// <summary>是否正在移动。</summary>
    public required bool Moving { get; init; }

    /// <summary>当前移动目标横坐标（km）。</summary>
    public required double TargetX { get; init; }

    /// <summary>当前移动目标纵坐标（km）。</summary>
    public required double TargetY { get; init; }

    /// <summary>是否被卡住（机动受阻）。</summary>
    public required bool Stuck { get; init; }
}
