// 文件总览：主窗口代码后置（应用壳）。
//
// 装配两个解耦循环：后台 SimulationPump（按时间控制档位步进模拟 tick）与
// UI DispatcherTimer（50ms 拉快照刷新面板）——渲染独立于模拟 tick
// （FR-025）。窗口关闭时停表并释放客户端句柄。

using System.Windows;
using System.Windows.Threading;
using WarFictionSim.Ui.GameControls;
using WarFictionSim.Ui.ViewModels;

namespace WarFictionSim.Ui;

/// <summary>主窗口。</summary>
public partial class MainWindow : Window
{
    private readonly DispatcherTimer _renderTimer;
    private SimulationPump? _pump;

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
        // 步进泵：每 50ms 由时间控制状态机决定步进数（暂停=0），在后台线程
        // 推进核心；渲染定时器只读快照，两者共享桥内锁保证句柄串行访问。
        _pump = new SimulationPump(
            () => ViewModel?.Game?.TimeControls.StepsPerFrame ?? 0,
            () => ViewModel?.Game?.StepOneTick(),
            TimeSpan.FromMilliseconds(50));
        _pump.Start();
        _renderTimer.Start();
    }

    private void RenderTimer_Tick(object? sender, EventArgs e) => ViewModel?.Game?.OnPresentationFrame();

    private void MainWindow_Closed(object? sender, EventArgs e)
    {
        _renderTimer.Stop();
        _pump?.Dispose();
        _pump = null;
        ViewModel?.Dispose();
    }
}
