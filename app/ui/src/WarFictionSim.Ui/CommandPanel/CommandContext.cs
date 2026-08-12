// 文件总览：命令面板 —— 校验上下文（T041）。
//
// 面板侧预校验需要与 native make_validation_context 相同的纯数据视图：
// 指挥节点、可指挥单位（id/节点/弹药）、已知区域、当前 tick。上下文由
// 快照 + 场景目录静态元数据合成（快照不含区域，区域来自场景文件）。

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>命令校验所需的单位纯数据视图（值相等：含弹药序列）。</summary>
public sealed class CommandableUnit : IEquatable<CommandableUnit>
{
    /// <summary>初始化单位视图。</summary>
    /// <param name="id">单位 id。</param>
    /// <param name="nodeId">所属指挥节点。</param>
    /// <param name="side">阵营。</param>
    /// <param name="ammoIds">单位装备弹药 id。</param>
    /// <param name="missionActive">是否已有执行中任务。</param>
    public CommandableUnit(string id, string nodeId, string side, IReadOnlyList<string> ammoIds, bool missionActive)
    {
        Id = id;
        NodeId = nodeId;
        Side = side;
        AmmoIds = ammoIds;
        MissionActive = missionActive;
    }

    /// <summary>单位 id。</summary>
    public string Id { get; }

    /// <summary>所属指挥节点。</summary>
    public string NodeId { get; }

    /// <summary>阵营。</summary>
    public string Side { get; }

    /// <summary>单位装备弹药 id。</summary>
    public IReadOnlyList<string> AmmoIds { get; }

    /// <summary>是否已有执行中任务。</summary>
    public bool MissionActive { get; }

    /// <inheritdoc />
    public bool Equals(CommandableUnit? other) =>
        other is not null &&
        Id == other.Id &&
        NodeId == other.NodeId &&
        Side == other.Side &&
        MissionActive == other.MissionActive &&
        AmmoIds.SequenceEqual(other.AmmoIds);

    /// <inheritdoc />
    public override bool Equals(object? obj) => obj is CommandableUnit other && Equals(other);

    /// <inheritdoc />
    public override int GetHashCode()
    {
        var hash = new HashCode();
        hash.Add(Id, StringComparer.Ordinal);
        hash.Add(NodeId, StringComparer.Ordinal);
        hash.Add(Side, StringComparer.Ordinal);
        hash.Add(MissionActive);
        foreach (string ammoId in AmmoIds)
        {
            hash.Add(ammoId, StringComparer.Ordinal);
        }

        return hash.ToHashCode();
    }
}

/// <summary>命令面板预校验上下文（只读）。</summary>
public sealed class CommandContext
{
    /// <summary>玩家指挥节点 id（空串 = 不做越权过滤，与核心语义一致）。</summary>
    public required string CommanderNodeId { get; init; }

    /// <summary>己方阵营标识。</summary>
    public required string FriendlySide { get; init; }

    /// <summary>当前游戏 tick（用于时限风险提示）。</summary>
    public required ulong CurrentTick { get; init; }

    /// <summary>场景全部单位（含敌方，用于 destroy_unit 目标引用校验）。</summary>
    public required IReadOnlyList<CommandableUnit> Units { get; init; }

    /// <summary>场景已知区域 id。</summary>
    public required IReadOnlyList<string> ZoneIds { get; init; }
}
