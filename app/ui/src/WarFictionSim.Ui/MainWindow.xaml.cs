// 文件总览：主窗口代码后置（应用壳）。
//
// 装配解耦的渲染循环：UI DispatcherTimer（50ms 拉快照刷新面板）——渲染
// 独立于模拟 tick（FR-025）。后台步进泵由 MainWindowViewModel 按战斗主屏
// 生命周期启停（N5），窗口关闭时停表并释放 ViewModel（含泵与客户端句柄）。

using System.Windows;
using System.Windows.Threading;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.ViewModels;

namespace WarFictionSim.Ui;

/// <summary>主窗口。</summary>
public partial class MainWindow : Window
{
    private readonly DispatcherTimer _renderTimer;

    /// <summary>初始化主窗口并装配表现层循环。</summary>
    public MainWindow()
    {
        InitializeComponent();
        _renderTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(50),
        };
        _renderTimer.Tick += RenderTimer_Tick;
        Loaded += MainWindow_Loaded;
        Closed += MainWindow_Closed;
    }

    private MainWindowViewModel? ViewModel => DataContext as MainWindowViewModel;

    private void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        // 渲染定时器只读快照；步进泵由 MainWindowViewModel 在进入战斗时
        // 启动、返回主菜单时停止（N5），两者共享桥内锁保证句柄串行访问。
        _renderTimer.Start();
    }

    private void RenderTimer_Tick(object? sender, EventArgs e)
    {
        try
        {
            ViewModel?.Game?.OnPresentationFrame();
        }
        catch (ObjectDisposedException)
        {
            // 返回主菜单/关窗竞态：战斗主屏已释放，渲染循环失去数据源，停表即可。
            _renderTimer.Stop();
        }
        catch (SnapshotParseException)
        {
            // 兜底：核心输出与快照契约漂移时，不再让 DispatcherTimer 未处理异常
            // 崩溃进程（正常路径由 GameScreenViewModel 呈现中文提示）。
            _renderTimer.Stop();
        }
    }

    private void MainWindow_Closed(object? sender, EventArgs e)
    {
        _renderTimer.Stop();
        ViewModel?.Dispose();
    }
}
