// 文件总览：兵牌地图 —— 单个兵牌的视图模型（T040）。
//
// 兵牌是地图上最小可观察单元：只持有表现字段（世界坐标、屏幕坐标、档位、
// 来源、选中态），更新入口 UpdateFrom 只接受纯数据 UnitMarkerData，
// 没有反向写回模拟的通道（宪法第 14 条）。

using CommunityToolkit.Mvvm.ComponentModel;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.BattleMap;

/// <summary>地图上一个可见兵牌。</summary>
public sealed partial class UnitMarkerViewModel : ObservableObject
{
    /// <summary>框选命中的兵牌半宽（px）：锚点向外扩展的近似兵牌边界（N4）。</summary>
    public const double ScreenHitHalfWidth = 12.0;

    /// <summary>框选命中的兵牌半高（px）：锚点向外扩展的近似兵牌边界（N4）。</summary>
    public const double ScreenHitHalfHeight = 10.0;

    private string _unitType = string.Empty;
    private string _displayName = string.Empty;
    private bool _isFriendly;
    private bool _isLastKnown;
    private double _worldX;
    private double _worldY;
    private double _screenX;
    private double _screenY;
    private IntelTier _tier;
    private string _sourceLabel = string.Empty;
    private double _motionDx;
    private double _motionDy;
    private bool _outOfContact;
    private bool _moving;
    private bool _isVehicle;
    private bool _isSelected;

    /// <summary>初始化兵牌。</summary>
    /// <param name="unitId">单位唯一 id。</param>
    public UnitMarkerViewModel(string unitId) => UnitId = unitId;

    /// <summary>单位唯一 id（不变）。</summary>
    public string UnitId { get; }

    /// <summary>单位类型 id。</summary>
    public string UnitType
    {
        get => _unitType;
        private set => SetProperty(ref _unitType, value);
    }

    /// <summary>按识别档位裁剪后的显示名（T1 不泄漏具体类型）。</summary>
    public string DisplayName
    {
        get => _displayName;
        private set => SetProperty(ref _displayName, value);
    }

    /// <summary>是否为己方单位。</summary>
    public bool IsFriendly
    {
        get => _isFriendly;
        private set => SetProperty(ref _isFriendly, value);
    }

    /// <summary>是否显示为"最后已知状态"（非实时）。</summary>
    public bool IsLastKnown
    {
        get => _isLastKnown;
        private set => SetProperty(ref _isLastKnown, value);
    }

    /// <summary>世界横坐标（km，最后已知时为最后目视位置）。</summary>
    public double WorldX
    {
        get => _worldX;
        private set => SetProperty(ref _worldX, value);
    }

    /// <summary>世界纵坐标（km）。</summary>
    public double WorldY
    {
        get => _worldY;
        private set => SetProperty(ref _worldY, value);
    }

    /// <summary>屏幕横坐标（px，由视口投影刷新）。</summary>
    public double ScreenX
    {
        get => _screenX;
        private set => SetProperty(ref _screenX, value);
    }

    /// <summary>屏幕纵坐标（px）。</summary>
    public double ScreenY
    {
        get => _screenY;
        private set => SetProperty(ref _screenY, value);
    }

    /// <summary>识别档位（敌方有效；己方为 None）。</summary>
    public IntelTier Tier
    {
        get => _tier;
        private set => SetProperty(ref _tier, value);
    }

    /// <summary>情报来源标注文案。</summary>
    public string SourceLabel
    {
        get => _sourceLabel;
        private set => SetProperty(ref _sourceLabel, value);
    }

    /// <summary>最后动向横向分量（归一化方向）。</summary>
    public double MotionDx
    {
        get => _motionDx;
        private set => SetProperty(ref _motionDx, value);
    }

    /// <summary>最后动向纵向分量（归一化方向）。</summary>
    public double MotionDy
    {
        get => _motionDy;
        private set => SetProperty(ref _motionDy, value);
    }

    /// <summary>己方单位是否失联。</summary>
    public bool OutOfContact
    {
        get => _outOfContact;
        private set => SetProperty(ref _outOfContact, value);
    }

    /// <summary>是否处于移动中（敌方仅表示"最后已知状态下仍在动"）。</summary>
    public bool Moving
    {
        get => _moving;
        private set => SetProperty(ref _moving, value);
    }

    /// <summary>是否为载具。</summary>
    public bool IsVehicle
    {
        get => _isVehicle;
        private set => SetProperty(ref _isVehicle, value);
    }

    /// <summary>是否被选中（命令面板执行单位）。</summary>
    public bool IsSelected
    {
        get => _isSelected;
        private set => SetProperty(ref _isSelected, value);
    }

    /// <summary>按纯数据刷新兵牌表现字段。</summary>
    /// <param name="data">可见兵牌数据。</param>
    public void UpdateFrom(UnitMarkerData data)
    {
        UnitType = data.UnitType;
        DisplayName = data.DisplayName;
        IsFriendly = data.IsFriendly;
        IsLastKnown = data.IsLastKnown;
        WorldX = data.X;
        WorldY = data.Y;
        Tier = data.Tier;
        SourceLabel = data.SourceLabel;
        MotionDx = data.MotionDx;
        MotionDy = data.MotionDy;
        OutOfContact = data.OutOfContact;
        Moving = data.Moving;
        IsVehicle = data.IsVehicle;
    }

    /// <summary>刷新屏幕坐标（由视口投影计算）。</summary>
    /// <param name="screenX">屏幕横坐标（px）。</param>
    /// <param name="screenY">屏幕纵坐标（px）。</param>
    public void SetScreenPosition(double screenX, double screenY)
    {
        ScreenX = screenX;
        ScreenY = screenY;
    }

    /// <summary>更新选中态。</summary>
    /// <param name="selected">是否选中。</param>
    public void SetSelected(bool selected) => IsSelected = selected;
}
