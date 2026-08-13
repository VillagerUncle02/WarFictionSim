// 文件总览：支援请求 UI —— 五种需求类型目录（T053）。
//
// 与 command-schema.md §1.1/§5 及 command.schema.json 的 request_type 枚举
// 完全一致（reinforce/fire_support/engineer/medical/logistics，FR-050）；
// 单选语义由视图下拉 + ViewModel 单一选择字段保证。

namespace WarFictionSim.Ui.SupportPanel;

/// <summary>支援需求类型选项。</summary>
/// <param name="Key">命令 JSON 的 request_type 值。</param>
/// <param name="DisplayName">中文显示名。</param>
public sealed record SupportRequestTypeOption(string Key, string DisplayName);

/// <summary>v1 五种支援需求类型（固定顺序，供下拉与测试确定性引用）。</summary>
public static class SupportRequestTypeCatalog
{
    /// <summary>全部需求类型。</summary>
    public static readonly IReadOnlyList<SupportRequestTypeOption> All =
    [
        new("reinforce", "加强兵力"),
        new("fire_support", "火力支援"),
        new("engineer", "工兵支援"),
        new("medical", "医疗支援"),
        new("logistics", "后勤支援"),
    ];
}
