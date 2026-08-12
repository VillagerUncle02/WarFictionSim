// 文件总览：命令面板视图代码后置（T041）。
//
// 只承载两处视图细节：类型下拉的 SelectionChanged 转发为"选类型带出默认
// 完成条件"命令；清空执行单位按钮。其余交互全部绑定 ViewModel。

using System.Windows;
using System.Windows.Controls;

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>命令面板视图。</summary>
public partial class CommandPanelView : UserControl
{
    /// <summary>初始化视图。</summary>
    public CommandPanelView() => InitializeComponent();

    private void TypeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (DataContext is CommandPanelViewModel viewModel &&
            TypeComboBox.SelectedValue is string typeKey)
        {
            viewModel.SetTypeCommand.Execute(typeKey);
        }
    }

    private void ClearExecutorsButton_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is CommandPanelViewModel viewModel)
        {
            viewModel.Draft.ExecutorIds.Clear();
        }
    }
}
