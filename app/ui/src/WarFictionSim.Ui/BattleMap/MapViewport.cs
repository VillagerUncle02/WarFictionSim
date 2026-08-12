// 文件总览：兵牌地图 —— 视口平移/缩放纯几何模型（T040）。
//
// 为什么从视图剥离：平移缩放是世界坐标(km)→屏幕坐标(px)的纯函数，独立于
// WPF 可单测，也保证渲染与模拟 tick 完全解耦（FR-025）——视口变化只影响
// 像素投影，绝不触碰快照数据。缩放有界、平移有界（地图不会完全移出视口）。

using CommunityToolkit.Mvvm.ComponentModel;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>地图视口（缩放倍数 + 平移偏移，纯几何）。</summary>
public sealed partial class MapViewport : ObservableObject
{
    private const double ZoomStepFactor = 1.25;

    private double _zoom;
    private double _offsetX;
    private double _offsetY;
    private double _viewportWidth;
    private double _viewportHeight;

    /// <summary>初始化视口。</summary>
    /// <param name="mapWidthKm">地图宽度（km，须为正）。</param>
    /// <param name="mapHeightKm">地图高度（km，须为正）。</param>
    /// <param name="minZoom">最小缩放（px/km）。</param>
    /// <param name="maxZoom">最大缩放（px/km）。</param>
    public MapViewport(double mapWidthKm, double mapHeightKm, double minZoom = 20, double maxZoom = 400)
    {
        if (mapWidthKm <= 0 || mapHeightKm <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(mapWidthKm), "地图尺寸必须为正。");
        }

        MapWidthKm = mapWidthKm;
        MapHeightKm = mapHeightKm;
        MinZoom = minZoom;
        MaxZoom = maxZoom;
        _zoom = minZoom;
    }

    /// <summary>地图宽度（km）。</summary>
    public double MapWidthKm { get; }

    /// <summary>地图高度（km）。</summary>
    public double MapHeightKm { get; }

    /// <summary>最小缩放（px/km）。</summary>
    public double MinZoom { get; }

    /// <summary>最大缩放（px/km）。</summary>
    public double MaxZoom { get; }

    /// <summary>当前缩放（px/km）。</summary>
    public double Zoom => _zoom;

    /// <summary>屏幕横向平移偏移（px）。</summary>
    public double OffsetX => _offsetX;

    /// <summary>屏幕纵向平移偏移（px）。</summary>
    public double OffsetY => _offsetY;

    /// <summary>视口宽度（px，由视图按实际尺寸同步）。</summary>
    public double ViewportWidth => _viewportWidth;

    /// <summary>视口高度（px）。</summary>
    public double ViewportHeight => _viewportHeight;

    /// <summary>世界坐标转屏幕坐标。</summary>
    /// <param name="x">世界横坐标（km）。</param>
    /// <param name="y">世界纵坐标（km）。</param>
    /// <returns>屏幕像素坐标。</returns>
    public (double X, double Y) WorldToScreen(double x, double y) =>
        ((x * _zoom) + _offsetX, (y * _zoom) + _offsetY);

    /// <summary>屏幕坐标转世界坐标（WorldToScreen 的逆变换）。</summary>
    /// <param name="screenX">屏幕横坐标（px）。</param>
    /// <param name="screenY">屏幕纵坐标（px）。</param>
    /// <returns>世界坐标（km）。</returns>
    public (double X, double Y) ScreenToWorld(double screenX, double screenY) =>
        ((screenX - _offsetX) / _zoom, (screenY - _offsetY) / _zoom);

    /// <summary>放大一档（围绕视口中心保持焦点）。</summary>
    public void ZoomIn() => SetZoom(_zoom * ZoomStepFactor);

    /// <summary>缩小一档（围绕视口中心保持焦点）。</summary>
    public void ZoomOut() => SetZoom(_zoom / ZoomStepFactor);

    /// <summary>设置绝对缩放并夹取到边界（围绕视口中心保持焦点）。</summary>
    /// <param name="zoom">目标缩放（px/km）。</param>
    public void SetZoom(double zoom)
    {
        double next = Math.Clamp(zoom, MinZoom, MaxZoom);
        if (Math.Abs(next - _zoom) < 1e-9)
        {
            return;
        }

        // 视口中心对应的世界点保持不变：缩放是"放大/缩小视野"，不是平移。
        double centerWorldX = (_viewportWidth / 2.0 - _offsetX) / _zoom;
        double centerWorldY = (_viewportHeight / 2.0 - _offsetY) / _zoom;
        _zoom = next;
        _offsetX = (_viewportWidth / 2.0) - (centerWorldX * _zoom);
        _offsetY = (_viewportHeight / 2.0) - (centerWorldY * _zoom);
        ClampOffsets();
        OnPropertyChanged(nameof(Zoom));
    }

    /// <summary>平移视口（屏幕像素增量，结果夹取在边界内）。</summary>
    /// <param name="deltaX">横向增量（px）。</param>
    /// <param name="deltaY">纵向增量（px）。</param>
    public void PanBy(double deltaX, double deltaY)
    {
        _offsetX += deltaX;
        _offsetY += deltaY;
        ClampOffsets();
    }

    /// <summary>同步视口像素尺寸并重新夹取边界。</summary>
    /// <param name="width">视口宽度（px）。</param>
    /// <param name="height">视口高度（px）。</param>
    public void SetViewportSize(double width, double height)
    {
        _viewportWidth = Math.Max(0, width);
        _viewportHeight = Math.Max(0, height);
        ClampOffsets();
    }

    /// <summary>重置为"整图适配视口并居中"的初始视野。</summary>
    public void ResetToFit()
    {
        if (_viewportWidth <= 0 || _viewportHeight <= 0)
        {
            _zoom = MinZoom;
            _offsetX = 0;
            _offsetY = 0;
            OnPropertyChanged(nameof(Zoom));
            return;
        }

        _zoom = Math.Clamp(
            Math.Min(_viewportWidth / MapWidthKm, _viewportHeight / MapHeightKm), MinZoom, MaxZoom);
        _offsetX = (_viewportWidth - (MapWidthKm * _zoom)) / 2.0;
        _offsetY = (_viewportHeight - (MapHeightKm * _zoom)) / 2.0;
        ClampOffsets();
        OnPropertyChanged(nameof(Zoom));
    }

    // 平移夹取：地图不得完全移出视口——当地图大于视口时偏移限制在
    // [视口 - 地图, 0]；地图小于视口时强制居中（无平移自由度）。
    private void ClampOffsets()
    {
        double mapWidthPx = MapWidthKm * _zoom;
        double mapHeightPx = MapHeightKm * _zoom;
        _offsetX = mapWidthPx >= _viewportWidth
            ? Math.Clamp(_offsetX, _viewportWidth - mapWidthPx, 0)
            : (_viewportWidth - mapWidthPx) / 2.0;
        _offsetY = mapHeightPx >= _viewportHeight
            ? Math.Clamp(_offsetY, _viewportHeight - mapHeightPx, 0)
            : (_viewportHeight - mapHeightPx) / 2.0;
        OnPropertyChanged(nameof(OffsetX));
        OnPropertyChanged(nameof(OffsetY));
    }
}
