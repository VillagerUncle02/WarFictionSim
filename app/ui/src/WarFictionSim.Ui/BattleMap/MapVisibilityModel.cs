// 文件总览：兵牌地图 —— 迷雾/识别档/来源/最后已知状态的纯解析（T040）。
//
// 为什么是纯函数：迷雾规则（FR-033/034/035、SC-005/006）必须与渲染/控件
// 解耦才能穷举测试。规则：己方始终可见（失联显示最后已知）；敌方只有
// 玩家节点的有效情报记录（档位 ≥ T1 且记忆未过期）才可见；识别档位决定
// 显示内容（T1 只显示"不明载具/步兵"，T2+ 显示具体类型）；来源标注按
// FR-031 映射；最后动向来自情报记录而非敌方实时状态（防泄漏）。

using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>一个可见兵牌的表现数据（纯数据，只读）。</summary>
public sealed record UnitMarkerData(
    string UnitId,
    string UnitType,
    string DisplayName,
    bool IsFriendly,
    bool IsLastKnown,
    double X,
    double Y,
    IntelTier Tier,
    string SourceLabel,
    double MotionDx,
    double MotionDy,
    bool OutOfContact,
    bool Moving,
    bool IsVehicle);

/// <summary>把快照解析为"玩家当前可见"的兵牌数据。</summary>
public static class MapVisibilityModel
{
    /// <summary>解析快照，返回仅含可见单位的兵牌数据。</summary>
    /// <param name="snapshot">只读快照。</param>
    /// <returns>可见兵牌列表（顺序与快照单位顺序一致，确定性）。</returns>
    public static IReadOnlyList<UnitMarkerData> Resolve(SimulationSnapshot snapshot)
    {
        string friendlySide = DeriveFriendlySide(snapshot);
        var markers = new List<UnitMarkerData>();
        foreach (UnitState unit in snapshot.Units)
        {
            if (unit.Side == friendlySide)
            {
                markers.Add(ResolveFriendly(unit));
                continue;
            }

            if (TryResolveEnemy(snapshot, unit, out UnitMarkerData? marker))
            {
                markers.Add(marker!);
            }
        }

        return markers;
    }

    /// <summary>把情报来源标注映射为玩家可读文案（FR-031）。</summary>
    /// <param name="source">情报来源。</param>
    /// <returns>来源标注文案。</returns>
    public static string ResolveSourceLabel(IntelSourceState source) => source.Kind switch
    {
        "direct" => $"直属发现：{source.UnitId}",
        "expired" => "来源已过期",
        "echelon" => $"上级转发：{source.NodeId}",
        "peer" => $"同级转报：{source.NodeId}",
        _ => $"来源：{source.Kind}",
    };

    private static string DeriveFriendlySide(SimulationSnapshot snapshot)
    {
        // 与核心 friendly_side 规则一致：player_node_id 单位的 side 兜底 node_id。
        foreach (UnitState unit in snapshot.Units)
        {
            if (unit.NodeId == snapshot.PlayerNodeId)
            {
                return unit.Side;
            }
        }

        return snapshot.PlayerNodeId;
    }

    private static UnitMarkerData ResolveFriendly(UnitState unit)
    {
        bool lastKnown = unit.OutOfContact && unit.HasLastKnown;
        return new UnitMarkerData(
            unit.Id,
            unit.Type,
            unit.Type,
            IsFriendly: true,
            IsLastKnown: lastKnown,
            X: lastKnown ? unit.LastKnownX : unit.X,
            Y: lastKnown ? unit.LastKnownY : unit.Y,
            Tier: IntelTier.None,
            SourceLabel: "己方单位",
            MotionDx: 0,
            MotionDy: 0,
            OutOfContact: unit.OutOfContact,
            Moving: unit.Moving,
            IsVehicle: unit.IsVehicle);
    }

    private static bool TryResolveEnemy(SimulationSnapshot snapshot, UnitState unit, out UnitMarkerData? marker)
    {
        marker = null;
        if (!snapshot.IntelRecords.TryGetValue($"{snapshot.PlayerNodeId}:{unit.Id}", out IntelRecordState? intel))
        {
            return false;
        }

        // 记忆保留语义与核心 step_intel 一致：tick >= memory_until_tick 删除；
        // 档位 None 表示从未达到 T1，不可见。
        if (snapshot.Tick >= intel.MemoryUntilTick || intel.Tier == IntelTier.None)
        {
            return false;
        }

        // 最后目视早于当前 tick：显示为"最后已知状态"并附最后动向（FR-035）。
        bool stale = intel.LastSeenTick < snapshot.Tick;
        string displayName = intel.Tier switch
        {
            IntelTier.T1 => unit.IsVehicle ? "不明载具" : "不明步兵单位",
            _ => unit.Type,
        };
        marker = new UnitMarkerData(
            unit.Id,
            unit.Type,
            displayName,
            IsFriendly: false,
            IsLastKnown: stale,
            X: intel.LastKnownX,
            Y: intel.LastKnownY,
            Tier: intel.Tier,
            SourceLabel: ResolveSourceLabel(intel.Source),
            MotionDx: intel.LastMotionDx,
            MotionDy: intel.LastMotionDy,
            OutOfContact: false,
            Moving: stale,
            IsVehicle: unit.IsVehicle);
        return true;
    }
}
