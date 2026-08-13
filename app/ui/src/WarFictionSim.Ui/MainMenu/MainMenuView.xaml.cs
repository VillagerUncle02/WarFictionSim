// 文件总览：主菜单视图代码后置（T039）。
//
// 只承载 WPF 专属交互：读档需要系统文件对话框（不属于 ViewModel 职责）。
// 其余逻辑全部在 MainMenuViewModel，便于单元测试。

using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace WarFictionSim.Ui.MainMenu;

/// <summary>主菜单视图。</summary>
public partial class MainMenuView : UserControl
{
    /// <summary>初始化视图。</summary>
    public MainMenuView() => InitializeComponent();

    private void LoadSaveButton_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "选择存档",
            Filter = "WarFictionSim 存档 (*.wfs)|*.wfs|所有文件 (*.*)|*.*",
            CheckFileExists = true,
        };
        if (dialog.ShowDialog() == true && DataContext is MainMenuViewModel viewModel)
        {
            viewModel.LoadSaveCommand.Execute(dialog.FileName);
        }
    }
}
