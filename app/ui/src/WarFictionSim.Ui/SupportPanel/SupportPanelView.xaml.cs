// 文件总览：支援请求视图代码后置（T053）。
//
// 只保留 InitializeComponent：全部交互（目标/类型/种类/数量/提交）绑定
// ViewModel，视图内不持有生命周期对象。

using System.Windows.Controls;

namespace WarFictionSim.Ui.SupportPanel;

/// <summary>支援请求视图。</summary>
public partial class SupportPanelView : UserControl
{
    /// <summary>初始化视图。</summary>
    public SupportPanelView() => InitializeComponent();
}
