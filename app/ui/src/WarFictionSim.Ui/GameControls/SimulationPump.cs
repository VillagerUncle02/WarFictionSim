// 文件总览：时间控制 —— 表现层步进泵（T042 + F3 生命周期加固）。
//
// 为什么在后台线程步进：模拟 tick 与渲染解耦（FR-025）——步进跑在线程池
// 定时器上，渲染循环只拉快照；档位由 StepsPerFrame 提供者决定（暂停=0），
// 泵本身不读现实时钟做结算（宪法第 16 条）。ISimClient 内部锁保证
// 步进线程与渲染线程共享句柄安全。
// 生命周期（F3）：Stop/Dispose 先停表再等待当前 step 结束，保证释放
// ISimClient 之后线程池回调不再触碰原生句柄；重叠的定时器回调被跳过。

namespace WarFictionSim.Ui.GameControls;

/// <summary>按表现层节奏调用模拟步进的后台泵。</summary>
public sealed class SimulationPump : IDisposable
{
    private readonly object _gate = new();
    private readonly Func<int> _stepsPerFrameProvider;
    private readonly Action _stepAction;
    private readonly System.Threading.Timer _timer;
    private bool _running;
    private bool _pumping;
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
    public void Start()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        lock (_gate)
        {
            _running = true;
        }

        _timer.Change(TimeSpan.Zero, Interval);
    }

    /// <summary>停止泵：先停表，再等待当前 step 结束（此后不再步进）。</summary>
    public void Stop()
    {
        lock (_gate)
        {
            _running = false;
        }

        _timer.Change(Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        WaitForPumpExit();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        // 释放顺序：先停止并等待在途 step 退出，再销毁计时器——
        // 保证调用方随后 Dispose ISimClient 时不再有线程池回调触碰原生句柄。
        Stop();
        lock (_gate)
        {
            _disposed = true;
        }

        _timer.Dispose();
    }

    private void PumpOnce()
    {
        lock (_gate)
        {
            // 已停止/已释放或已有回调在途：本周期跳过（重叠回调不得并发步进）。
            if (!_running || _pumping)
            {
                return;
            }

            _pumping = true;
        }

        try
        {
            // 步进数由表现层状态机给出；核心每步仍是精确 1 tick。
            int steps = Math.Max(0, _stepsPerFrameProvider());
            for (int index = 0; index < steps; index++)
            {
                lock (_gate)
                {
                    if (!_running)
                    {
                        return;
                    }
                }

                _stepAction();
            }
        }
        finally
        {
            lock (_gate)
            {
                _pumping = false;
                Monitor.PulseAll(_gate);
            }
        }
    }

    private void WaitForPumpExit()
    {
        lock (_gate)
        {
            while (_pumping)
            {
                Monitor.Wait(_gate);
            }
        }
    }
}
