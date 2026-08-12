// 文件总览：时间控制 —— 速度档位定义（T042）。
//
// FR-027：档位 1x/2x/4x/8x，暂停为独立状态。档位只是表现层"每帧步进数"
// 的倍数，不改变模拟 tick 大小（宪法第 16 条：现实时间加速只允许在表现层）。

namespace WarFictionSim.Ui.GameControls;

/// <summary>模拟速度档位。</summary>
public static class TimeScales
{
    /// <summary>默认模拟 tick 频率（20 Hz，plan.md 固定值）。</summary>
    public const int DefaultTickHz = 20;

    /// <summary>可选档位（升序）。</summary>
    public static readonly IReadOnlyList<int> All = [1, 2, 4, 8];

    /// <summary>判断倍率是否为合法档位。</summary>
    /// <param name="multiplier">倍率。</param>
    /// <returns>是否在 1x/2x/4x/8x 内。</returns>
    public static bool IsValid(int multiplier) => All.Contains(multiplier);
}
