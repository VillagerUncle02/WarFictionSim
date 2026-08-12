// 文件总览：战斗主屏视图代码后置（应用壳）。
//
// 只保留 InitializeComponent；计时器/步进泵由 MainWindow 装配，
// 视图内不持有生命周期对象。

using System.Windows.Controls;

namespace WarFictionSim.Ui.ViewModels;

/// <summary>战斗主屏视图。</summary>
public partial class GameScreenView : UserControl
{
    /// <summary>初始化视图。</summary>
    public GameScreenView() => InitializeComponent();
}
