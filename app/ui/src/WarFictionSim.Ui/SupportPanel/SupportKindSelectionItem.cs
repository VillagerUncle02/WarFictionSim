// 文件总览：支援请求 UI —— 支援种类多选条目（T053）。
//
// CheckBox 需要可观察的选中态（多选）；条目只包装资源池只读选项，
// 不携带任何模拟状态（宪法第 14 条：面板状态与核心状态分离）。

using CommunityToolkit.Mvvm.ComponentModel;

namespace WarFictionSim.Ui.SupportPanel;

/// <summary>支援种类多选条目（IsSelected 变化通知面板重算扣减与校验）。</summary>
public sealed partial class SupportKindSelectionItem : ObservableObject
{
    private bool _isSelected;

    /// <summary>初始化条目。</summary>
    /// <param name="option">资源池只读选项。</param>
    public SupportKindSelectionItem(SupportKindOption option) => Option = option;

    /// <summary>选中态变化事件（视图模型据此重校验/重算分数）。</summary>
    public event EventHandler? Toggled;

    /// <summary>资源池选项（只读）。</summary>
    public SupportKindOption Option { get; }

    /// <summary>种类 id。</summary>
    public string Id => Option.Id;

    /// <summary>展示名。</summary>
    public string DisplayName => Option.DisplayName;

    /// <summary>是否已勾选。</summary>
    public bool IsSelected
    {
        get => _isSelected;
        set
        {
            if (SetProperty(ref _isSelected, value))
            {
                Toggled?.Invoke(this, EventArgs.Empty);
            }
        }
    }
}
