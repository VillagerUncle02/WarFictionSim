// 文件总览：兵牌地图视图代码后置（T040）。
//
// 只承载 WPF 输入事件（滚轮缩放、拖拽平移、尺寸同步）：把鼠标动作翻译为
// MapViewport 的纯几何调用，并刷新兵牌屏幕坐标。点击选中兵牌走 DataTemplate
// 里的命令绑定，不在这里处理（避免把选择逻辑塞进视图）。

using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>2D 兵牌地图视图。</summary>
public partial class BattleMapView : UserControl
{
    private Point? _lastDragPoint;

    /// <summary>初始化视图。</summary>
    public BattleMapView() => InitializeComponent();

    private void ZoomInButton_Click(object sender, RoutedEventArgs e) => Zoom(zoomIn: true);

    private void ZoomOutButton_Click(object sender, RoutedEventArgs e) => Zoom(zoomIn: false);

    private void FitButton_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is BattleMapViewModel viewModel)
        {
            viewModel.Viewport.ResetToFit();
            viewModel.RefreshPositions();
        }
    }

    private void Zoom(bool zoomIn)
    {
        if (DataContext is not BattleMapViewModel viewModel)
        {
            return;
        }

        if (zoomIn)
        {
            viewModel.Viewport.ZoomIn();
        }
        else
        {
            viewModel.Viewport.ZoomOut();
        }

        viewModel.RefreshPositions();
    }

    private void MapCanvas_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (DataContext is BattleMapViewModel viewModel)
        {
            viewModel.Viewport.SetViewportSize(e.NewSize.Width, e.NewSize.Height);
            viewModel.RefreshPositions();
        }
    }

    private void MapCanvas_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (DataContext is BattleMapViewModel viewModel)
        {
            if (e.Delta > 0)
            {
                viewModel.Viewport.ZoomIn();
            }
            else
            {
                viewModel.Viewport.ZoomOut();
            }

            viewModel.RefreshPositions();
            e.Handled = true;
        }
    }

    private void MapCanvas_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        // 点击兵牌按钮时不要启动拖拽平移（按钮自己处理选中）。
        if (e.OriginalSource is System.Windows.DependencyObject source &&
            FindAncestor<Button>(source) is not null)
        {
            return;
        }

        _lastDragPoint = e.GetPosition(MapCanvas);
        MapCanvas.CaptureMouse();
    }

    private void MapCanvas_MouseMove(object sender, MouseEventArgs e)
    {
        if (_lastDragPoint is not Point previous || DataContext is not BattleMapViewModel viewModel)
        {
            return;
        }

        Point current = e.GetPosition(MapCanvas);
        viewModel.Viewport.PanBy(current.X - previous.X, current.Y - previous.Y);
        viewModel.RefreshPositions();
        _lastDragPoint = current;
    }

    private void MapCanvas_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        _lastDragPoint = null;
        MapCanvas.ReleaseMouseCapture();
    }

    private static T? FindAncestor<T>(DependencyObject source)
        where T : DependencyObject
    {
        DependencyObject? current = source;
        while (current is not null)
        {
            if (current is T match)
            {
                return match;
            }

            current = System.Windows.Media.VisualTreeHelper.GetParent(current);
        }

        return null;
    }
}
