// 文件总览：主菜单 —— XAML 值转换器（T039）。
//
// 为什么用轻量静态转换器而非第三方库：规模 RadioButton 与错误文本可见性
// 是两处简单映射，避免为视图引入大型框架（plan 未列入）而拖累足迹预算。

using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace WarFictionSim.Ui.MainMenu;

/// <summary>作战规模 ↔ RadioButton IsChecked 转换器（每个规模一个静态实例）。</summary>
public sealed class CombatScaleToBooleanConverter : IValueConverter
{
    /// <summary>连排级实例。</summary>
    public static readonly CombatScaleToBooleanConverter Platoon = new(CombatScale.Platoon);

    /// <summary>营级实例。</summary>
    public static readonly CombatScaleToBooleanConverter Battalion = new(CombatScale.Battalion);

    private readonly CombatScale _scale;

    private CombatScaleToBooleanConverter(CombatScale scale) => _scale = scale;

    /// <inheritdoc />
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is CombatScale scale && scale == _scale;

    /// <inheritdoc />
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true ? _scale : Binding.DoNothing;
}

/// <summary>bool → Visibility 转换器（供错误文本显示）。</summary>
public sealed class BooleanToVisibilityConverterEx : IValueConverter
{
    /// <summary>共享实例（XAML 用 x:Static 引用）。</summary>
    public static readonly BooleanToVisibilityConverterEx Default = new();

    /// <inheritdoc />
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true ? Visibility.Visible : Visibility.Collapsed;

    /// <inheritdoc />
    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException("此转换器只用于单向绑定。");
}
