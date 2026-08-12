// 文件总览：兵牌地图 —— 地图视图模型（T040）。
//
// 渲染独立于模拟 tick（FR-025）：ApplySnapshot 只做"快照 → 可见兵牌"的
// 就地投影（增量更新集合，避免每帧整体重建），视口变化只重算屏幕坐标；
// 本类型没有任何写回模拟的调用（宪法第 14 条）。选择经事件外发，
// 供命令面板消费。

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>2D 兵牌地图视图模型。</summary>
public sealed partial class BattleMapViewModel : ObservableObject
{
    private string? _selectedUnitId;
    private readonly HashSet<string> _selectedUnitIds = new(StringComparer.Ordinal);

    /// <summary>初始化地图。</summary>
    /// <param name="mapWidthKm">地图宽度（km）。</param>
    /// <param name="mapHeightKm">地图高度（km）。</param>
    public BattleMapViewModel(double mapWidthKm, double mapHeightKm)
    {
        Viewport = new MapViewport(mapWidthKm, mapHeightKm);
        SelectUnitCommand = new RelayCommand<string?>(SelectUnit);
    }

    /// <summary>当玩家在地图上点选兵牌时触发（参数为单位 id；清空为 null）。</summary>
    public event EventHandler<string?>? UnitSelected;

    /// <summary>当玩家在地图上框选一批兵牌时触发（参数为命中的单位 id 列表）。</summary>
    public event EventHandler<IReadOnlyList<string>>? UnitsSelected;

    /// <summary>视口（平移/缩放）。</summary>
    public MapViewport Viewport { get; }

    /// <summary>当前可见兵牌。</summary>
    public ObservableCollection<UnitMarkerViewModel> Markers { get; } = [];

    /// <summary>当前选中单位 id（无选中为 null）。</summary>
    public string? SelectedUnitId
    {
        get => _selectedUnitId;
        private set => SetProperty(ref _selectedUnitId, value);
    }

    /// <summary>当前选中单位 id 集合（点选 1 个、框选多个；只读快照）。</summary>
    public IReadOnlyCollection<string> SelectedUnitIds => _selectedUnitIds;

    /// <summary>点选兵牌命令。</summary>
    public IRelayCommand<string?> SelectUnitCommand { get; }

    /// <summary>把快照投影为可见兵牌（增量更新）。</summary>
    /// <param name="snapshot">只读快照。</param>
    public void ApplySnapshot(SimulationSnapshot snapshot)
    {
        IReadOnlyList<UnitMarkerData> resolved = MapVisibilityModel.Resolve(snapshot);
        var existing = new Dictionary<string, UnitMarkerViewModel>(Markers.Count);
        foreach (UnitMarkerViewModel marker in Markers)
        {
            existing[marker.UnitId] = marker;
        }

        var visibleIds = new HashSet<string>(resolved.Count);
        foreach (UnitMarkerData data in resolved)
        {
            visibleIds.Add(data.UnitId);
            if (existing.TryGetValue(data.UnitId, out UnitMarkerViewModel? marker))
            {
                marker.UpdateFrom(data);
            }
            else
            {
                var created = new UnitMarkerViewModel(data.UnitId);
                created.UpdateFrom(data);
                Markers.Add(created);
            }
        }

        for (int index = Markers.Count - 1; index >= 0; index--)
        {
            if (!visibleIds.Contains(Markers[index].UnitId))
            {
                Markers.RemoveAt(index);
            }
        }

        List<string> stillVisible = _selectedUnitIds.Where(visibleIds.Contains).ToList();
        if (stillVisible.Count != _selectedUnitIds.Count)
        {
            // 迷雾把选中单位移出视野：只保留仍可见者，按剩余数量选择事件形态。
            SetSelection(stillVisible, raiseUnitSelected: stillVisible.Count <= 1, raiseBatch: stillVisible.Count > 1);
        }

        RefreshPositions();
    }

    /// <summary>点选/清空选中单位。</summary>
    /// <param name="unitId">单位 id；null 清空选择。</param>
    public void SelectUnit(string? unitId)
    {
        if (unitId is not null && Markers.All(marker => marker.UnitId != unitId))
        {
            return; // 不可见单位不能被选中（迷雾下不能指挥看不到的单位）。
        }

        // 点选语义：重复点选唯一选中单位时取消；否则替换为单选该单位。
        if (unitId is not null && _selectedUnitIds.SetEquals([unitId]))
        {
            SetSelection([], raiseUnitSelected: true, raiseBatch: false);
            return;
        }

        SetSelection(unitId is null ? [] : [unitId], raiseUnitSelected: true, raiseBatch: false);
    }

    /// <summary>框选屏幕矩形内的兵牌（默认只取己方单位，供命令执行单位派生）。</summary>
    /// <param name="left">屏幕矩形左边界（px）。</param>
    /// <param name="top">屏幕矩形上边界（px）。</param>
    /// <param name="right">屏幕矩形右边界（px）。</param>
    /// <param name="bottom">屏幕矩形下边界（px）。</param>
    /// <param name="friendlyOnly">是否只选中己方兵牌。</param>
    public void SelectUnitsInScreenRect(double left, double top, double right, double bottom, bool friendlyOnly = true)
    {
        List<string> hitIds = Markers
            .Where(marker => marker.ScreenX >= left && marker.ScreenX <= right &&
                             marker.ScreenY >= top && marker.ScreenY <= bottom)
            .Where(marker => !friendlyOnly || marker.IsFriendly)
            .Select(marker => marker.UnitId)
            .ToList();
        SetSelection(hitIds, raiseUnitSelected: false, raiseBatch: true);
    }

    /// <summary>清空全部选中（Esc 语义）。</summary>
    public void ClearSelection() => SelectUnit(null);

    private void SetSelection(IReadOnlyCollection<string> unitIds, bool raiseUnitSelected, bool raiseBatch)
    {
        _selectedUnitIds.Clear();
        foreach (string unitId in unitIds)
        {
            _selectedUnitIds.Add(unitId);
        }

        SelectedUnitId = _selectedUnitIds.Count == 1 ? _selectedUnitIds.Single() : null;
        foreach (UnitMarkerViewModel marker in Markers)
        {
            marker.SetSelected(_selectedUnitIds.Contains(marker.UnitId));
        }

        if (raiseUnitSelected)
        {
            UnitSelected?.Invoke(this, SelectedUnitId);
        }

        if (raiseBatch)
        {
            UnitsSelected?.Invoke(this, unitIds.ToList());
        }
    }

    /// <summary>按当前视口重算全部兵牌屏幕坐标。</summary>
    public void RefreshPositions()
    {
        foreach (UnitMarkerViewModel marker in Markers)
        {
            (double screenX, double screenY) = Viewport.WorldToScreen(marker.WorldX, marker.WorldY);
            marker.SetScreenPosition(screenX, screenY);
        }
    }
}
