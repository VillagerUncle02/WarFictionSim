// 文件总览：兵牌地图视图代码后置（T040 + F2 命令闭环）。
//
// 只承载 WPF 输入事件翻译：滚轮缩放、右键拖拽平移、左键点选兵牌
// （命中 UnitMarkerViewModel 的 Button 命令）、左键拖拽出选择矩形框选
// （命中矩形内单位）、Esc 清除选择。选择几何命中判定在
// BattleMapViewModel.SelectUnitsInScreenRect（可单测），视图只传递矩形。

using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>2D 兵牌地图视图。</summary>
public partial class BattleMapView : UserControl
{
    private const double ClickDragThreshold = 4.0;

    private Point? _selectionStart;
    private bool _isBoxSelecting;
    private Point? _panPoint;

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

    private void UserControl_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape && DataContext is BattleMapViewModel viewModel)
        {
            viewModel.ClearSelection();
            e.Handled = true;
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
        // 点击兵牌按钮时不要启动框选/清空（按钮自己处理点选）。
        if (e.OriginalSource is System.Windows.DependencyObject source &&
            FindAncestor<Button>(source) is not null)
        {
            return;
        }

        _selectionStart = e.GetPosition(MapCanvas);
        _isBoxSelecting = false;
        UpdateSelectionRect(_selectionStart.Value, _selectionStart.Value);
        MapCanvas.CaptureMouse();
    }

    private void MapCanvas_MouseMove(object sender, MouseEventArgs e)
    {
        if (_selectionStart is Point start && DataContext is BattleMapViewModel selectionViewModel)
        {
            Point current = e.GetPosition(MapCanvas);
            if (!_isBoxSelecting)
            {
                double distance = (current - start).Length;
                if (distance < ClickDragThreshold)
                {
                    return; // 尚属点选，未进入框选。
                }

                _isBoxSelecting = true;
                SelectionRect.Visibility = Visibility.Visible;
            }

            UpdateSelectionRect(start, current);
            return;
        }

        if (_panPoint is Point previous && DataContext is BattleMapViewModel panViewModel)
        {
            Point current = e.GetPosition(MapCanvas);
            panViewModel.Viewport.PanBy(current.X - previous.X, current.Y - previous.Y);
            panViewModel.RefreshPositions();
            _panPoint = current;
        }
    }

    private void MapCanvas_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (_selectionStart is not Point start)
        {
            return;
        }

        if (DataContext is BattleMapViewModel viewModel)
        {
            if (_isBoxSelecting)
            {
                Point end = e.GetPosition(MapCanvas);
                viewModel.SelectUnitsInScreenRect(
                    Math.Min(start.X, end.X),
                    Math.Min(start.Y, end.Y),
                    Math.Max(start.X, end.X),
                    Math.Max(start.Y, end.Y));
            }
            else
            {
                viewModel.SelectUnit(null); // 点空白处清除选择。
            }
        }

        _selectionStart = null;
        _isBoxSelecting = false;
        SelectionRect.Visibility = Visibility.Collapsed;
        MapCanvas.ReleaseMouseCapture();
    }

    private void MapCanvas_MouseRightButtonDown(object sender, MouseButtonEventArgs e)
    {
        _panPoint = e.GetPosition(MapCanvas);
        MapCanvas.CaptureMouse();
    }

    private void MapCanvas_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
    {
        _panPoint = null;
        MapCanvas.ReleaseMouseCapture();
    }

    private void UpdateSelectionRect(Point start, Point current)
    {
        Canvas.SetLeft(SelectionRect, Math.Min(start.X, current.X));
        Canvas.SetTop(SelectionRect, Math.Min(start.Y, current.Y));
        SelectionRect.Width = Math.Abs(current.X - start.X);
        SelectionRect.Height = Math.Abs(current.Y - start.Y);
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
