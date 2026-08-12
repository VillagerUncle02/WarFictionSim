// 文件总览：时间控制 —— 表现层步进泵（T042）。
//
// 为什么在后台线程步进：模拟 tick 与渲染解耦（FR-025）——步进跑在线程池
// 定时器上，渲染循环只拉快照；档位由 StepsPerFrame 提供者决定（暂停=0），
// 泵本身不读现实时钟做结算（宪法第 16 条）。ISimClient 内部锁保证
// 步进线程与渲染线程共享句柄安全。

namespace WarFictionSim.Ui.GameControls;

/// <summary>按表现层节奏调用模拟步进的后台泵。</summary>
public sealed class SimulationPump : IDisposable
{
    private readonly Func<int> _stepsPerFrameProvider;
    private readonly Action _stepAction;
    private readonly System.Threading.Timer _timer;
    private bool _disposed;

    /// <summary>初始化泵。</summary>
    /// <param name="stepsPerFrameProvider">返回本帧步进数（暂停返回 0）。</param>
    /// <param name="stepAction">单个 tick 的步进动作（一般调 ISimClient.Step）。</param>
    /// <param name="interval">表现层帧间隔。</param>
    public SimulationPump(Func<int> stepsPerFrameProvider, Action stepAction, TimeSpan interval)
    {
        _stepsPerFrameProvider = stepsPerFrameProvider;
        _stepAction = stepAction;
        _timer = new System.Threading.Timer(
            _ => PumpOnce(), null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        Interval = interval;
    }

    /// <summary>表现层帧间隔。</summary>
    public TimeSpan Interval { get; }

    /// <summary>启动泵。</summary>
    public void Start() => _timer.Change(TimeSpan.Zero, Interval);

    /// <summary>停止泵（暂停计时器，不销毁）。</summary>
    public void Stop() => _timer.Change(Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);

    /// <inheritdoc />
    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _timer.Dispose();
    }

    private void PumpOnce()
    {
        if (_disposed)
        {
            return;
        }

        // 步进数由表现层状态机给出；核心每步仍是精确 1 tick。
        int steps = Math.Max(0, _stepsPerFrameProvider());
        for (int index = 0; index < steps && !_disposed; index++)
        {
            _stepAction();
        }
    }
}
