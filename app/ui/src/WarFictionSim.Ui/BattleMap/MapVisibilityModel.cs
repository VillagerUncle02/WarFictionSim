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
        // 核心稳定词汇 direct|sync|relay|expired（native/sim/include/wfs/sim/intel.h）。
        "sync" => $"同级经上级同步：{source.UnitId}",
        "relay" => $"上级转发：{source.NodeId}",
        "expired" => "来源已过期",
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
        string displayName = BuildDisplayName(intel, unit);
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

    /// <summary>按识别档位渲染显示名（FR-034：T1 数量、T2 类型、T3 型号/构成）。</summary>
    /// <param name="intel">情报记录。</param>
    /// <param name="unit">目标单位运行期状态（仅用于档位化字段缺失时的可得信息兜底）。</param>
    /// <returns>识别档位裁剪后的显示名。</returns>
    private static string BuildDisplayName(IntelRecordState intel, UnitState unit)
    {
        // TODO(F5/核心 T058 后置)：native intel_records 快照尚未输出
        // observed_count/type_name/composition 档位化字段，当前只能显示
        // 可得信息（不明步兵/载具、数据目录 type id），核心补齐字段后
        // 下方分支自动生效；不编造数量/类型/构成。
        return intel.Tier switch
        {
            IntelTier.T1 when intel.ObservedCount is { } count =>
                unit.IsVehicle ? $"不明载具（约 {count} 辆）" : $"不明步兵（约 {count} 人）",
            IntelTier.T1 => unit.IsVehicle ? "不明载具" : "不明步兵单位",
            IntelTier.T2 => intel.TypeName ?? unit.Type,
            IntelTier.T3 => intel.TypeName is { } type && intel.Composition is { } composition
                ? $"{type}（{composition}）"
                : intel.TypeName ?? intel.Composition ?? unit.Type,
            _ => unit.Type,
        };
    }
}
