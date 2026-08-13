// 文件总览：时间控制 —— 速度档 ↔ RadioButton IsChecked 转换器（T042）。
//
// 每个档位一个静态实例，避免为单选高亮引入样式框架；只读映射，保持轻量。

using System.Globalization;
using System.Windows.Data;

namespace WarFictionSim.Ui.GameControls;

/// <summary>int 相等性 → bool 转换器（用于速度档单选高亮）。</summary>
public sealed class IntEqualsConverter : IValueConverter
{
    /// <summary>1x 实例。</summary>
    public static readonly IntEqualsConverter One = new(1);

    /// <summary>2x 实例。</summary>
    public static readonly IntEqualsConverter Two = new(2);

    /// <summary>4x 实例。</summary>
    public static readonly IntEqualsConverter Four = new(4);

    /// <summary>8x 实例。</summary>
    public static readonly IntEqualsConverter Eight = new(8);

    private readonly int _value;

    private IntEqualsConverter(int value) => _value = value;

    /// <inheritdoc />
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is int number && number == _value;

    /// <inheritdoc />
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true ? _value : Binding.DoNothing;
}
