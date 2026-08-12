// 文件总览：时间控制 —— 暂停/加速状态机（T042）。
//
// 暂停是独立状态：暂停不清除当前速度档，恢复后沿用；加速只改变
// StepsPerFrame（每表现帧步进数）与 EffectiveTickInterval（表现层计时），
// 不触碰模拟状态——本类型完全不依赖 ISimClient，从类型层面杜绝
// "加速改写模拟 tick"（宪法第 16 条）。

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace WarFictionSim.Ui.GameControls;

/// <summary>暂停/加速控制视图模型。</summary>
public sealed partial class TimeControlsViewModel : ObservableObject
{
    private bool _isPaused;
    private int _speedMultiplier = 1;

    /// <summary>初始化时间控制。</summary>
    /// <param name="tickHz">模拟 tick 频率（须 ≥1）。</param>
    public TimeControlsViewModel(int tickHz = TimeScales.DefaultTickHz)
    {
        if (tickHz < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(tickHz), "tick 频率必须 ≥ 1。");
        }

        TickHz = tickHz;
        TogglePauseCommand = new RelayCommand(TogglePause);
        SetSpeedCommand = new RelayCommand<int>(SetSpeed);
        CycleSpeedCommand = new RelayCommand(CycleSpeed);
    }

    /// <summary>模拟 tick 频率（Hz）。</summary>
    public int TickHz { get; }

    /// <summary>是否暂停（独立状态，不影响速度档）。</summary>
    public bool IsPaused
    {
        get => _isPaused;
        private set
        {
            if (SetProperty(ref _isPaused, value))
            {
                OnPropertyChanged(nameof(StepsPerFrame));
                OnPropertyChanged(nameof(EffectiveTickInterval));
                OnPropertyChanged(nameof(StatusText));
            }
        }
    }

    /// <summary>当前速度档（1/2/4/8）。</summary>
    public int SpeedMultiplier
    {
        get => _speedMultiplier;
        private set
        {
            if (SetProperty(ref _speedMultiplier, value))
            {
                OnPropertyChanged(nameof(StepsPerFrame));
                OnPropertyChanged(nameof(EffectiveTickInterval));
                OnPropertyChanged(nameof(StatusText));
            }
        }
    }

    /// <summary>每个表现帧应步进的模拟 tick 数（暂停为 0）。</summary>
    public int StepsPerFrame => IsPaused ? 0 : SpeedMultiplier;

    /// <summary>表现层基础帧间隔（固定 50ms = 20Hz，与档位无关）。</summary>
    public TimeSpan FrameInterval => TimeSpan.FromMilliseconds(1000.0 / TimeScales.DefaultTickHz);

    /// <summary>当前档位下每个 tick 对应的现实时间（暂停时为无限）。</summary>
    public TimeSpan EffectiveTickInterval => IsPaused
        ? Timeout.InfiniteTimeSpan
        : TimeSpan.FromSeconds(1.0 / TickHz / SpeedMultiplier);

    /// <summary>状态栏文案（"已暂停"或"4x"）。</summary>
    public string StatusText => IsPaused ? "已暂停" : $"{SpeedMultiplier}x";

    /// <summary>暂停/继续切换。</summary>
    public IRelayCommand TogglePauseCommand { get; }

    /// <summary>设置速度档（参数 1/2/4/8）。</summary>
    public IRelayCommand<int> SetSpeedCommand { get; }

    /// <summary>按 1→2→4→8→1 循环切换速度档。</summary>
    public IRelayCommand CycleSpeedCommand { get; }

    /// <summary>暂停。</summary>
    public void Pause() => IsPaused = true;

    /// <summary>继续（沿用暂停前的速度档）。</summary>
    public void Resume() => IsPaused = false;

    /// <summary>暂停/继续切换。</summary>
    public void TogglePause() => IsPaused = !IsPaused;

    /// <summary>设置速度档。</summary>
    /// <param name="multiplier">1/2/4/8。</param>
    public void SetSpeed(int multiplier)
    {
        if (!TimeScales.IsValid(multiplier))
        {
            throw new ArgumentOutOfRangeException(nameof(multiplier), "速度档只支持 1x/2x/4x/8x。");
        }

        SpeedMultiplier = multiplier;
    }

    /// <summary>循环切换速度档。</summary>
    public void CycleSpeed()
    {
        int index = 0;
        for (; index < TimeScales.All.Count; index++)
        {
            if (TimeScales.All[index] == SpeedMultiplier)
            {
                break;
            }
        }

        SpeedMultiplier = TimeScales.All[(index + 1) % TimeScales.All.Count];
    }
}
