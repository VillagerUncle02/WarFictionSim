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

        if (SelectedUnitId is not null && !visibleIds.Contains(SelectedUnitId))
        {
            SelectUnit(null);
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

        SelectedUnitId = unitId;
        foreach (UnitMarkerViewModel marker in Markers)
        {
            marker.SetSelected(marker.UnitId == unitId);
        }

        UnitSelected?.Invoke(this, unitId);
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
