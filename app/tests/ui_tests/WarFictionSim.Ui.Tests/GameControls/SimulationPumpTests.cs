// 测试：时间控制 —— 步进泵的停止/释放生命周期（F3）。
//
// 断言 Stop/Dispose 先停止计时器并等待当前 step 结束：不能让线程池回调
// 在 ISimClient.Dispose 之后继续调用 wfs_sim_step（原生 use-after-free），
// 也不能让重叠的定时器回调并发步进。

using WarFictionSim.Ui.GameControls;
using Xunit;

namespace WarFictionSim.Ui.Tests.GameControls;

public class SimulationPumpTests
{
    [Fact]
    public async Task Stop_WaitsForInFlightStep_AndStopsFurtherSteps()
    {
        using var stepStarted = new ManualResetEventSlim();
        using var allowFinish = new ManualResetEventSlim();
        int stepCalls = 0;
        using var pump = new SimulationPump(
            () => 1,
            () =>
            {
                stepCalls++;
                stepStarted.Set();
                allowFinish.Wait(TimeSpan.FromSeconds(5));
            },
            TimeSpan.FromMilliseconds(1));
        pump.Start();
        Assert.True(stepStarted.Wait(TimeSpan.FromSeconds(2)));

        var stopCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Task stopTask = Task.Run(() =>
        {
            pump.Stop();
            stopCompleted.SetResult();
        });
        Task firstFinished = await Task.WhenAny(stopTask, Task.Delay(100));
        Assert.NotSame(stopTask, firstFinished); // 在途 step 未结束前 Stop 必须阻塞等待。

        allowFinish.Set();
        await stopCompleted.Task.WaitAsync(TimeSpan.FromSeconds(2));
        int callsAtStop = stepCalls;
        Thread.Sleep(60);
        Assert.Equal(callsAtStop, stepCalls); // 停止后不再步进。
    }

    [Fact]
    public async Task Dispose_WaitsForInFlightStep_ThenRejectsNewWork()
    {
        using var stepStarted = new ManualResetEventSlim();
        using var allowFinish = new ManualResetEventSlim();
        int stepCalls = 0;
        var pump = new SimulationPump(
            () => 1,
            () =>
            {
                stepCalls++;
                stepStarted.Set();
                allowFinish.Wait(TimeSpan.FromSeconds(5));
            },
            TimeSpan.FromMilliseconds(1));
        pump.Start();
        Assert.True(stepStarted.Wait(TimeSpan.FromSeconds(2)));

        var disposeCompleted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Task disposeTask = Task.Run(() =>
        {
            pump.Dispose();
            disposeCompleted.SetResult();
        });
        Task firstFinished = await Task.WhenAny(disposeTask, Task.Delay(100));
        Assert.NotSame(disposeTask, firstFinished);

        allowFinish.Set();
        await disposeCompleted.Task.WaitAsync(TimeSpan.FromSeconds(2));
        int callsAtDispose = stepCalls;
        Thread.Sleep(60);
        Assert.Equal(callsAtDispose, stepCalls);
    }

    [Fact]
    public void Start_AfterStop_ResumesStepping()
    {
        using var pump = new SimulationPump(() => 1, () => Interlocked.Increment(ref _stepCount), TimeSpan.FromMilliseconds(1));
        pump.Start();
        Thread.Sleep(40);
        pump.Stop();
        int stoppedCount = Volatile.Read(ref _stepCount);
        Thread.Sleep(40);
        Assert.Equal(stoppedCount, Volatile.Read(ref _stepCount));

        pump.Start();
        Thread.Sleep(40);
        Assert.True(Volatile.Read(ref _stepCount) > stoppedCount);
    }

    private int _stepCount;
}
