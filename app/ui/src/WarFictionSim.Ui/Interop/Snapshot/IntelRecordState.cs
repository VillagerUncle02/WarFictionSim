// 文件总览：UI↔核心互操作 —— 情报记录 DTO（T043，只读消费）。
//
// 字段镜像 native/sim/src/intel.cpp IntelRecord/IntelSource 的 to_json 输出；
// 识别档位（T1–T3）、来源标注、最后已知位置与最后动向是兵牌地图（T040）
// 的直接数据源，解析为强类型枚举便于迷雾层判定。observed_count/type_name/
// composition 为可选档位化观察字段（FR-034 信息内容；核心当前尚未输出，
// 见 MapVisibilityModel 的 TODO——缺字段时 UI 只显示可得信息，不编造）。

namespace WarFictionSim.Ui.Interop;

/// <summary>目标识别档位（FR-034：T1 最低、T3 最高；None 表示不可见）。</summary>
public enum IntelTier
{
    /// <summary>不可见/未识别。</summary>
    None,

    /// <summary>最低档：步兵数量 / 载具存在。</summary>
    T1,

    /// <summary>中档：载具类型 / 步兵装备线索。</summary>
    T2,

    /// <summary>最高档：具体型号与构成。</summary>
    T3,
}

/// <summary>情报来源标注（FR-031：具体发现单位或来源层级）。</summary>
public sealed record IntelSourceState(
    string Kind,
    string UnitId,
    string NodeId,
    ulong ReportedTick);

/// <summary>某观察节点对某目标单位的情报记录（只读）。</summary>
public sealed record IntelRecordState(
    string ObserverNodeId,
    string TargetUnitId,
    IntelTier Tier,
    ulong LastSeenTick,
    ulong MemoryUntilTick,
    ulong SourceExpiresTick,
    IntelSourceState Source,
    double LastKnownX,
    double LastKnownY,
    double LastMotionDx,
    double LastMotionDy,
    ulong? ObservedCount = null,
    string? TypeName = null,
    string? Composition = null);
