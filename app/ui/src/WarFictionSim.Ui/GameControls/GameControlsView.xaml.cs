// 文件总览：时间控制视图代码后置（T042）。
//
// 全部交互绑定 TimeControlsViewModel 命令，本文件只保留 InitializeComponent；
// 视图层不持有任何计时器（步进泵由应用壳装配）。

using System.Windows.Controls;

namespace WarFictionSim.Ui.GameControls;

/// <summary>暂停/加速控制视图。</summary>
public partial class GameControlsView : UserControl
{
    /// <summary>初始化视图。</summary>
    public GameControlsView() => InitializeComponent();
}
