// 文件总览：应用壳 —— 主窗口视图模型（菜单 ↔ 战斗导航与客户端生命周期）。
//
// 主菜单只产出启动请求；这里完成真正的句柄创建（读档在进入前执行）并持有
// ISimClient 生命周期。创建/读档失败回填主菜单内联错误，绝不静默（宪法 17）。
// 步进泵生命周期随战斗主屏（N5）：进入战斗启动、返回主菜单先停泵再释放
// 句柄（等待在途 step），不在主菜单空转。

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using WarFictionSim.Ui.GameControls;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.MainMenu;

namespace WarFictionSim.Ui.ViewModels;

/// <summary>主窗口视图模型。</summary>
public sealed partial class MainWindowViewModel : ObservableObject, IDisposable
{
    private readonly ISimClientFactory _clientFactory;
    private SimulationPump? _pump;
    private object? _currentView;
    private bool _disposed;

    /// <summary>初始化主窗口。</summary>
    /// <param name="catalog">场景目录。</param>
    /// <param name="clientFactory">模拟客户端工厂。</param>
    public MainWindowViewModel(IScenarioCatalog catalog, ISimClientFactory clientFactory)
    {
        _clientFactory = clientFactory;
        Menu = new MainMenuViewModel(catalog);
        Menu.GameStartRequested += OnGameStartRequested;
        BackToMenuCommand = new RelayCommand(BackToMenu, () => Game is not null);
        _currentView = Menu;
    }

    /// <summary>主菜单视图模型。</summary>
    public MainMenuViewModel Menu { get; }

    /// <summary>当前战斗主屏（未开始时为 null）。</summary>
    public GameScreenViewModel? Game { get; private set; }

    /// <summary>当前视图（主菜单或战斗主屏），由 DataTemplate 选择对应视图。</summary>
    public object? CurrentView
    {
        get => _currentView;
        private set => SetProperty(ref _currentView, value);
    }

    /// <summary>返回主菜单命令。</summary>
    public IRelayCommand BackToMenuCommand { get; }

    /// <summary>返回主菜单（释放战斗句柄）。</summary>
    public void BackToMenu()
    {
        DisposeGame();
        CurrentView = Menu;
        BackToMenuCommand.NotifyCanExecuteChanged();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Menu.GameStartRequested -= OnGameStartRequested;
        // 释放顺序：先停泵（等待在途 step）再释放句柄，与 F3/N5 一致。
        _pump?.Dispose();
        _pump = null;
        Game?.Dispose();
        Game = null;
    }

    private void OnGameStartRequested(object? sender, GameStartRequestedEventArgs e)
    {
        try
        {
            ISimClient client = _clientFactory.Create(e.Request.Scenario.Path, e.Request.Seed, e.Request.Threads);
            if (e.Request.SavePath is not null)
            {
                client.LoadSave(e.Request.SavePath);
            }

            DisposeGame();
            Game = new GameScreenViewModel(client, e.Request.Scenario);
            StartPump();
            CurrentView = Game;
            BackToMenuCommand.NotifyCanExecuteChanged();
        }
        catch (SimNativeException exception)
        {
            Menu.ShowError(exception.Message);
        }
    }

    /// <summary>启动随战斗主屏运行的表现层步进泵（复用已停止的实例）。</summary>
    private void StartPump()
    {
        _pump ??= new SimulationPump(
            () => Game?.TimeControls.StepsPerFrame ?? 0,
            () => Game?.StepOneTick(),
            TimeSpan.FromMilliseconds(50));
        _pump.Start();
    }

    private void DisposeGame()
    {
        // 先停泵（等待在途 step 退出），再释放 ISimClient 句柄——返回主菜单
        // 后泵不再空转，也不会有线程池回调触碰已释放的原生句柄（F3/N5）。
        _pump?.Stop();
        Game?.Dispose();
        Game = null;
    }
}
