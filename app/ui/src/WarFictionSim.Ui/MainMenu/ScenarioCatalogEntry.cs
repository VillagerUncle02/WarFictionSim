// 文件总览：主菜单 —— 场景目录条目（T039，只读）。
//
// 条目是目录对场景 JSON 的强类型投影：主菜单（规模过滤/节点选择）、
// 战斗地图（地图尺寸/区域）、命令面板（指挥节点/区域上下文）都从这里
// 取静态元数据，避免各处重复解析场景文件。

namespace WarFictionSim.Ui.MainMenu;

using WarFictionSim.Ui.SupportPanel;

/// <summary>可玩场景的静态元数据（只读）。</summary>
public sealed class ScenarioCatalogEntry
{
    /// <summary>场景唯一 id。</summary>
    public required string Id { get; init; }

    /// <summary>场景显示名。</summary>
    public required string Name { get; init; }

    /// <summary>场景 JSON 文件路径。</summary>
    public required string Path { get; init; }

    /// <summary>场景默认玩家指挥节点（空串表示未指定）。</summary>
    public required string PlayerNodeId { get; init; }

    /// <summary>是否为教程场景（教程使用独立存档槽）。</summary>
    public required bool IsTutorial { get; init; }

    /// <summary>教程独立存档槽名（非教程为 <see langword="null"/>）。</summary>
    public required string? SaveSlot { get; init; }

    /// <summary>作战规模。</summary>
    public required CombatScale Scale { get; init; }

    /// <summary>场景随机种子。</summary>
    public required ulong Seed { get; init; }

    /// <summary>模拟 tick 频率（Hz）。</summary>
    public required uint TickHz { get; init; }

    /// <summary>地图宽度（km）。</summary>
    public required double MapWidthKm { get; init; }

    /// <summary>地图高度（km）。</summary>
    public required double MapHeightKm { get; init; }

    /// <summary>场景区域 id 列表（命令面板区域目标候选）。</summary>
    public required IReadOnlyList<string> Zones { get; init; }

    /// <summary>玩家可扮演的己方指挥节点 id（按阵营派生）。</summary>
    public required IReadOnlyList<string> CommandNodeIds { get; init; }

    /// <summary>场景是否配置支援管线（T053；false = 支援面板给出未配置提示）。</summary>
    public bool SupportConfigured { get; init; }

    /// <summary>支援规模（platoon/battalion；来自 support.scale）。</summary>
    public string SupportScale { get; init; } = string.Empty;

    /// <summary>受理上级节点（support.superior_node_id；to_node 缺省值）。</summary>
    public string SuperiorNodeId { get; init; } = string.Empty;

    /// <summary>可请求支援种类池（派系资源池投影；缺失时目录记录问题并留空）。</summary>
    public IReadOnlyList<SupportKindOption> SupportKinds { get; init; } = [];
}
