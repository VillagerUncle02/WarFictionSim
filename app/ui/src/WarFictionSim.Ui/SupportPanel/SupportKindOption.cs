// 文件总览：支援请求 UI —— 支援种类选项与面板静态元数据（T053）。
//
// 支援种类来自请求方所属营级编制的派系资源池（FR-008：只能请求所属上级
// 组织资源池内的力量），由场景目录在加载时投影（ScenarioCatalog），
// 面板与三级校验共用同一份只读清单；成本用于有限分数预估扣减
// （cost = quantity × Σ条目成本，与 native deduct_score 同式）。

namespace WarFictionSim.Ui.SupportPanel;

/// <summary>资源池中的一种可请求支援种类（只读）。</summary>
/// <param name="Id">支援种类 id（资源池条目 id，如 squad-mortar-team）。</param>
/// <param name="Cost">单份成本（连排级分数扣减用）。</param>
/// <param name="Kind">条目类别（unit/fire_support 等，供展示与语义区分）。</param>
public sealed record SupportKindOption(string Id, ulong Cost, string Kind)
{
    /// <summary>展示名：稳定 id 目前直接可读，随数据目录扩展后补中文映射。</summary>
    public string DisplayName => Id;
}

/// <summary>支援面板静态元数据（由场景目录投影，随快照帧动态叠加分数）。</summary>
public sealed class SupportPanelOptions
{
    /// <summary>场景是否配置支援管线（未配置时给出提示，不静默）。</summary>
    public bool Configured { get; init; }

    /// <summary>支援规模（platoon/battalion；决定扣分还是配属链）。</summary>
    public string Scale { get; init; } = string.Empty;

    /// <summary>受理上级节点（to_node 缺省值，核心同规则兜底）。</summary>
    public string SuperiorNodeId { get; init; } = string.Empty;

    /// <summary>可请求支援种类池（空 = 池缺失/未配置，面板给出提示）。</summary>
    public IReadOnlyList<SupportKindOption> AvailableKinds { get; init; } = [];
}
