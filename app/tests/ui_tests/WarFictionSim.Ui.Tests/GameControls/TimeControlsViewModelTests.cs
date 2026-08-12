// 测试：时间控制 —— 速度档位与暂停状态机（T042）。
//
// FR-027/宪法第 16 条：加速只发生在表现层——档位改变的是"每帧步进数/
// 表现层计时"，模拟 tick 语义不变；暂停是独立状态，不重置速度档。

using WarFictionSim.Ui.GameControls;
using Xunit;

namespace WarFictionSim.Ui.Tests.GameControls;

public class TimeControlsViewModelTests
{
    [Fact]
    public void Defaults_AreOneXAndRunning()
    {
        var viewModel = new TimeControlsViewModel();

        Assert.False(viewModel.IsPaused);
        Assert.Equal(1, viewModel.SpeedMultiplier);
        Assert.Equal(1, viewModel.StepsPerFrame);
    }

    [Fact]
    public void AvailableScales_AreOneTwoFourEight()
    {
        Assert.Equal([1, 2, 4, 8], TimeScales.All);
    }

    [Fact]
    public void SetSpeed_ValidMultiplier_UpdatesStepsPerFrame()
    {
        var viewModel = new TimeControlsViewModel();

        viewModel.SetSpeedCommand.Execute(4);

        Assert.Equal(4, viewModel.SpeedMultiplier);
        Assert.Equal(4, viewModel.StepsPerFrame);
        Assert.False(viewModel.IsPaused);
    }

    [Fact]
    public void SetSpeed_InvalidMultiplier_Throws()
    {
        var viewModel = new TimeControlsViewModel();

        Assert.Throws<ArgumentOutOfRangeException>(() => viewModel.SetSpeedCommand.Execute(3));
    }

    [Fact]
    public void CycleSpeed_WrapsOneToEight()
    {
        var viewModel = new TimeControlsViewModel();

        viewModel.CycleSpeedCommand.Execute(null);
        Assert.Equal(2, viewModel.SpeedMultiplier);
        viewModel.CycleSpeedCommand.Execute(null);
        Assert.Equal(4, viewModel.SpeedMultiplier);
        viewModel.CycleSpeedCommand.Execute(null);
        Assert.Equal(8, viewModel.SpeedMultiplier);
        viewModel.CycleSpeedCommand.Execute(null);
        Assert.Equal(1, viewModel.SpeedMultiplier);
    }

    [Fact]
    public void Pause_IsIndependentState_AndKeepsSpeed()
    {
        var viewModel = new TimeControlsViewModel();
        viewModel.SetSpeedCommand.Execute(4);

        viewModel.TogglePauseCommand.Execute(null);
        Assert.True(viewModel.IsPaused);
        Assert.Equal(0, viewModel.StepsPerFrame);
        Assert.Equal(4, viewModel.SpeedMultiplier); // 暂停不清除速度档。

        viewModel.TogglePauseCommand.Execute(null);
        Assert.False(viewModel.IsPaused);
        Assert.Equal(4, viewModel.SpeedMultiplier); // 恢复后沿用原速度档。
        Assert.Equal(4, viewModel.StepsPerFrame);
    }

    [Fact]
    public void EffectiveTickInterval_HalvesAtDoubleSpeed()
    {
        var viewModel = new TimeControlsViewModel();

        TimeSpan atOneX = viewModel.EffectiveTickInterval;
        viewModel.SetSpeedCommand.Execute(2);

        Assert.Equal(50, atOneX.TotalMilliseconds, 1);
        Assert.Equal(25, viewModel.EffectiveTickInterval.TotalMilliseconds, 1);
    }

    [Fact]
    public void PresentationFrameInterval_StaysFixedAcrossSpeeds()
    {
        var viewModel = new TimeControlsViewModel();

        TimeSpan before = viewModel.FrameInterval;
        viewModel.SetSpeedCommand.Execute(8);

        // 表现层帧节奏固定（20Hz 基础帧），加速只增加每帧步进数——
        // 模拟 tick 语义不变（宪法第 16 条）。
        Assert.Equal(before, viewModel.FrameInterval);
        Assert.Equal(8, viewModel.StepsPerFrame);
    }

    [Fact]
    public void Constructor_WithInvalidTickHz_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new TimeControlsViewModel(tickHz: 0));
    }
}
