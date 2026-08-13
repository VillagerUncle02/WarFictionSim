// 文件总览：事件日志面板视图代码后置（T042 + N1 滚动锚点）。
//
// 交互全部绑定 EventLogViewModel；本文件额外承载滚动位置保留：ViewModel
// 全量重建 Entries 前外发 ScrollAnchorChanged（重建前最后一个展示条目），
// 视图据此捕获最后可见条目的 seq，并在重建完成后 ScrollIntoView 恢复位置。

using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件日志面板视图。</summary>
public partial class EventLogPanelView : UserControl
{
    private ulong? _pendingScrollAnchor;

    /// <summary>初始化视图。</summary>
    public EventLogPanelView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (e.OldValue is EventLogViewModel previous)
        {
            previous.ScrollAnchorChanged -= OnScrollAnchorChanged;
        }

        if (e.NewValue is EventLogViewModel viewModel)
        {
            viewModel.ScrollAnchorChanged += OnScrollAnchorChanged;
        }
    }

    private void OnScrollAnchorChanged(object? sender, ulong? fallbackSeq)
    {
        // 事件在 Clear 之前触发：此刻仍能读到最后可见条目的 seq；读不到时退回
        // ViewModel 提供的最后一个展示条目。重建是同步的批量 Clear+Add，
        // 因此恢复放到 Background 优先级，确保全部 Add 已进入集合。
        _pendingScrollAnchor = CaptureLastVisibleSeq() ?? fallbackSeq;
        Dispatcher.BeginInvoke(DispatcherPriority.Background, RestoreScrollAnchor);
    }

    private void RestoreScrollAnchor()
    {
        if (_pendingScrollAnchor is not ulong anchor)
        {
            return;
        }

        _pendingScrollAnchor = null;
        EventLogEntryViewModel? target = EntriesList.Items.OfType<EventLogEntryViewModel>()
            .LastOrDefault(entry => entry.Seq <= anchor)
            ?? EntriesList.Items.OfType<EventLogEntryViewModel>()
                .FirstOrDefault(entry => entry.Seq >= anchor);
        if (target is not null)
        {
            EntriesList.ScrollIntoView(target);
        }
    }

    private ulong? CaptureLastVisibleSeq()
    {
        ScrollViewer? scrollViewer = FindVisualChild<ScrollViewer>(EntriesList);
        if (scrollViewer is null)
        {
            return null;
        }

        for (int index = EntriesList.Items.Count - 1; index >= 0; index--)
        {
            if (EntriesList.ItemContainerGenerator.ContainerFromIndex(index) is not FrameworkElement container ||
                EntriesList.Items[index] is not EventLogEntryViewModel entry)
            {
                continue;
            }

            Point topLeft = container.TranslatePoint(new Point(0, 0), scrollViewer);
            if (topLeft.Y >= 0 && topLeft.Y < scrollViewer.ViewportHeight)
            {
                return entry.Seq;
            }
        }

        return null;
    }

    private static T? FindVisualChild<T>(DependencyObject parent)
        where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            DependencyObject child = VisualTreeHelper.GetChild(parent, index);
            if (child is T match)
            {
                return match;
            }

            if (FindVisualChild<T>(child) is T descendant)
            {
                return descendant;
            }
        }

        return null;
    }
}
