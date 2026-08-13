// 文件总览：兵牌地图 —— 识别档位 → 可见性转换器（T040）。
//
// 己方兵牌档位为 None，不应显示"[None]"徽标；敌方仅 T1–T3 显示档位。
// 该映射只属于视图层，故用轻量转换器而非塞进 ViewModel。

using System.Globalization;
using System.Windows;
using System.Windows.Data;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>IntelTier → Visibility（None 隐藏，其余可见）。</summary>
public sealed class IntelTierToVisibilityConverter : IValueConverter
{
    /// <summary>共享实例。</summary>
    public static readonly IntelTierToVisibilityConverter Default = new();

    /// <inheritdoc />
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is IntelTier tier && tier != IntelTier.None ? Visibility.Visible : Visibility.Collapsed;

    /// <inheritdoc />
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException("此转换器只用于单向绑定。");
}
