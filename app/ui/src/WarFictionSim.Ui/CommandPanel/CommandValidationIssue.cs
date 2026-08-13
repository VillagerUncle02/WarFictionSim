// 文件总览：命令面板 —— 三级校验提示的数据类型（T041）。
//
// 三级分级术语与 FR-045 统一：错误（阻断提交）、警告（可提交但提示风险）、
// 建议（上下文提示）。所有提示渠道（命令面板/事件提示/简报/错误处理）
// 共用同一套语义，视觉呈现由视图按 Severity 映射。

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>提示严重级（FR-045 统一分级术语）。</summary>
public enum CommandIssueSeverity
{
    /// <summary>错误：阻断提交，必须修正。</summary>
    Error,

    /// <summary>警告：可提交但提示风险（如超限/冲突）。</summary>
    Warning,

    /// <summary>建议：上下文提示（如可选行为参数）。</summary>
    Suggestion,
}

/// <summary>一条内嵌校验提示。</summary>
/// <param name="Severity">严重级。</param>
/// <param name="Code">稳定错误码（与 native 校验码风格一致）。</param>
/// <param name="Message">中文提示文本。</param>
public sealed record CommandValidationIssue(CommandIssueSeverity Severity, string Code, string Message);

/// <summary>三级校验结果。</summary>
public sealed class CommandValidationResult
{
    /// <summary>初始化结果。</summary>
    /// <param name="issues">按固定检查顺序收集的提示。</param>
    public CommandValidationResult(IReadOnlyList<CommandValidationIssue> issues) => Issues = issues;

    /// <summary>全部提示（固定顺序）。</summary>
    public IReadOnlyList<CommandValidationIssue> Issues { get; }

    /// <summary>是否含阻断错误。</summary>
    public bool HasErrors => Issues.Any(issue => issue.Severity == CommandIssueSeverity.Error);

    /// <summary>是否含警告。</summary>
    public bool HasWarnings => Issues.Any(issue => issue.Severity == CommandIssueSeverity.Warning);
}
