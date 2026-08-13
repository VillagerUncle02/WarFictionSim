// 文件总览：命令面板 —— 正在编辑的命令草稿（T041）。
//
// 草稿是面板内部的可变编辑状态，映射 contracts/command-schema.md §1 的全部
// 字段；只有经三级校验通过后，CommandJsonBuilder 才把它序列化为不可变
// JSON 文本交给核心——草稿本身不是模拟状态（宪法第 14 条）。

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>命令草稿（可观察，随输入变化驱动三级校验）。</summary>
public sealed partial class CommandDraft : ObservableObject
{
    private string? _type;
    private string? _condition;
    private string? _zoneId;
    private string? _targetUnitId;
    private double? _pointX;
    private double? _pointY;
    private double? _exitPointX;
    private double? _exitPointY;
    private long? _durationTicks;
    private long? _cycleTicks;
    private long? _constructionTicks;
    private string _intent = string.Empty;
    private string _engagement = "balanced";
    private string? _formation;
    private string? _ammoOverride;
    private string _failureAction = "report";
    private string? _failureTarget;
    private long _priority;
    private ulong _deadlineTick;
    private string? _supportRequestType;
    private long? _supportQuantity;
    private string? _supportToNode;
    private string? _supportForCommandId;

    /// <summary>命令类型（13 种任务之一；未选择为 null）。</summary>
    public string? Type
    {
        get => _type;
        set => SetProperty(ref _type, value);
    }

    /// <summary>完成条件（由类型默认填充，可手动调整）。</summary>
    public string? Condition
    {
        get => _condition;
        set => SetProperty(ref _condition, value);
    }

    /// <summary>执行单位（点选=1 个 → kind=unit；框选=多个 → kind=units）。</summary>
    public ObservableCollection<string> ExecutorIds { get; } = [];

    /// <summary>区域目标 id（secure_zone/drive_out/clear）。</summary>
    public string? ZoneId
    {
        get => _zoneId;
        set => SetProperty(ref _zoneId, value);
    }

    /// <summary>单位目标 id（destroy_unit）。</summary>
    public string? TargetUnitId
    {
        get => _targetUnitId;
        set => SetProperty(ref _targetUnitId, value);
    }

    /// <summary>目标点横坐标（km，reach_point/recon）。</summary>
    public double? PointX
    {
        get => _pointX;
        set => SetProperty(ref _pointX, value);
    }

    /// <summary>目标点纵坐标（km）。</summary>
    public double? PointY
    {
        get => _pointY;
        set => SetProperty(ref _pointY, value);
    }

    /// <summary>渗透撤离点横坐标（recon 可选）。</summary>
    public double? ExitPointX
    {
        get => _exitPointX;
        set => SetProperty(ref _exitPointX, value);
    }

    /// <summary>渗透撤离点纵坐标（recon 可选）。</summary>
    public double? ExitPointY
    {
        get => _exitPointY;
        set => SetProperty(ref _exitPointY, value);
    }

    /// <summary>驻留时长（tick，hold/secure_zone）。</summary>
    public long? DurationTicks
    {
        get => _durationTicks;
        set => SetProperty(ref _durationTicks, value);
    }

    /// <summary>巡逻周期（tick，patrol）。</summary>
    public long? CycleTicks
    {
        get => _cycleTicks;
        set => SetProperty(ref _cycleTicks, value);
    }

    /// <summary>构筑时长（tick，fortify）。</summary>
    public long? ConstructionTicks
    {
        get => _constructionTicks;
        set => SetProperty(ref _constructionTicks, value);
    }

    /// <summary>意图/附加说明（自由文本，供展示与 AI 理解）。</summary>
    public string Intent
    {
        get => _intent;
        set => SetProperty(ref _intent, value);
    }

    /// <summary>接敌策略（aggressive/balanced/cautious）。</summary>
    public string Engagement
    {
        get => _engagement;
        set => SetProperty(ref _engagement, value);
    }

    /// <summary>队形覆盖（march/combat；null = 按命令类型自动）。</summary>
    public string? Formation
    {
        get => _formation;
        set => SetProperty(ref _formation, value);
    }

    /// <summary>任务级弹药覆盖（null = 自动选弹）。</summary>
    public string? AmmoOverride
    {
        get => _ammoOverride;
        set => SetProperty(ref _ammoOverride, value);
    }

    /// <summary>失败后处置（withdraw_to/hold/report）。</summary>
    public string FailureAction
    {
        get => _failureAction;
        set => SetProperty(ref _failureAction, value);
    }

    /// <summary>withdraw_to 的撤退目标 "x,y"。</summary>
    public string? FailureTarget
    {
        get => _failureTarget;
        set => SetProperty(ref _failureTarget, value);
    }

    /// <summary>优先级（0 为最低；同优先级按到达顺序裁决）。</summary>
    public long Priority
    {
        get => _priority;
        set => SetProperty(ref _priority, value);
    }

    /// <summary>时限（游戏 tick；0 视为未设置并触发警告）。</summary>
    public ulong DeadlineTick
    {
        get => _deadlineTick;
        set => SetProperty(ref _deadlineTick, value);
    }

    /// <summary>支援请求需求类型（SUPPORT_REQUEST，5 种枚举之一）。</summary>
    public string? SupportRequestType
    {
        get => _supportRequestType;
        set => SetProperty(ref _supportRequestType, value);
    }

    /// <summary>支援请求种类多选（资源池条目 id；null = 未选）。</summary>
    public ObservableCollection<string> SupportKinds { get; } = [];

    /// <summary>每种支援种类的数量（≥1；null = 缺省 1，schema 允许缺省）。</summary>
    public long? SupportQuantity
    {
        get => _supportQuantity;
        set => SetProperty(ref _supportQuantity, value);
    }

    /// <summary>受理上级节点（null = 缺省取场景支援配置 superior_node_id）。</summary>
    public string? SupportToNode
    {
        get => _supportToNode;
        set => SetProperty(ref _supportToNode, value);
    }

    /// <summary>关联的任务命令 id（任务结束触发归建，FR-009；null = 不关联）。</summary>
    public string? SupportForCommandId
    {
        get => _supportForCommandId;
        set => SetProperty(ref _supportForCommandId, value);
    }
}
