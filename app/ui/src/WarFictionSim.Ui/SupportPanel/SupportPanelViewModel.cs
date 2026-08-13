// 文件总览：支援请求 UI —— 视图模型（T053）。
//
// 流程：应用壳每帧喂入快照 + 己方阵营 → 面板只读消费 support 摘要
// （score_remaining/pending_requests/attaches）并过滤出己方可指挥单位 →
// 选目标/需求类型/支援种类/数量 → 三级校验（错误阻断、警告提示）→
// CommandJsonBuilder 序列化 SUPPORT_REQUEST → 提交事件交给应用壳经
// ISimClient 注入核心（命令注入是唯一写路径，宪法第 14 条）；核心拒绝
// （最终权威）回填内嵌错误。
// 请求状态由事件查询派生（SUPPORT_REQUESTED→EVALUATING→ASSIGNED/
// REJECTED、归建/转请，SupportStatusMapper），查询按快照 tick 前进节流，
// 查询失败保留上次状态并显示中文错误（不向渲染循环抛异常）。
// 数值输入走字符串包装属性：非法输入以 NUMERIC_INPUT_INVALID 内嵌提示，
// 草稿只在解析成功后更新（FR-045 不静默、不打断操作）。

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using WarFictionSim.Ui.CommandPanel;
using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.SupportPanel;

/// <summary>支援请求面板视图模型。</summary>
public sealed partial class SupportPanelViewModel : ObservableObject
{
    /// <summary>运行中快照 tick 前进时的状态查询节流间隔（避免每帧两连查）。</summary>
    public static readonly TimeSpan StatusRefreshThrottle = TimeSpan.FromMilliseconds(500);

    private readonly SupportPanelOptions _options;
    private readonly ISimClient? _client;
    private readonly TimeProvider _timeProvider;
    private readonly Dictionary<string, CommandValidationIssue> _inputIssues = new(StringComparer.Ordinal);

    private string _friendlySide = string.Empty;
    private string _commanderNodeId = string.Empty;
    private IReadOnlyList<CommandableUnit> _friendlyUnits = [];
    private ulong _snapshotTick;
    private ulong _scoreRemaining;
    private ulong _pendingRequests;
    private ulong _attaches;
    private string? _selectedTargetUnitId;
    private string? _selectedRequestType;
    private string _quantityText = "1";
    private long? _quantity = 1;
    private string _toNodeText;
    private string _forCommandIdText = string.Empty;
    private string _priorityText = "0";
    private long _priority;
    private string _deadlineTickText = "0";
    private ulong _deadlineTick;
    private string _statusText = "暂无支援请求状态";
    private SupportRequestStatus _status = SupportRequestStatus.None;
    private bool _canSubmit;
    private ulong _lastSyncedTick = ulong.MaxValue;
    private DateTimeOffset _lastStatusPullAt = DateTimeOffset.MinValue;

    /// <summary>初始化支援请求面板。</summary>
    /// <param name="options">场景投影的支援静态元数据（可用池/上级节点/规模）。</param>
    /// <param name="client">模拟客户端（生产注入；null 时不查询事件状态）。</param>
    /// <param name="timeProvider">现实时间源（测试注入节流；默认系统时钟）。</param>
    public SupportPanelViewModel(
        SupportPanelOptions options,
        ISimClient? client = null,
        TimeProvider? timeProvider = null)
    {
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _client = client;
        _timeProvider = timeProvider ?? TimeProvider.System;
        _toNodeText = options.SuperiorNodeId;
        RequestTypes = SupportRequestTypeCatalog.All;
        TargetUnits = new ObservableCollection<CommandableUnit>();
        KindOptions = new ObservableCollection<SupportKindSelectionItem>();
        foreach (SupportKindOption kind in options.AvailableKinds)
        {
            var item = new SupportKindSelectionItem(kind);
            // 勾选变化统一走重算扣减 + 重校验，视图与测试共用同一入口。
            item.Toggled += (_, _) =>
            {
                RecomputeScore();
                Revalidate();
            };
            KindOptions.Add(item);
        }

        Issues = new ObservableCollection<CommandValidationIssue>();
        SelectTargetCommand = new RelayCommand<string?>(SetTarget);
        SelectRequestTypeCommand = new RelayCommand<string?>(SetRequestType);
        SubmitCommand = new RelayCommand(Submit, () => CanSubmit);
    }

    /// <summary>校验通过并序列化成功时触发（由应用壳经 ISimClient 注入核心）。</summary>
    public event EventHandler<string>? SubmitRequested;

    /// <summary>己方可指挥单位（快照过滤：同阵营 + 玩家节点范围内）。</summary>
    public ObservableCollection<CommandableUnit> TargetUnits { get; }

    /// <summary>可用支援种类多选条目（来自所属编制资源池）。</summary>
    public ObservableCollection<SupportKindSelectionItem> KindOptions { get; }

    /// <summary>五种需求类型目录（单选）。</summary>
    public IReadOnlyList<SupportRequestTypeOption> RequestTypes { get; }

    /// <summary>内嵌三级校验提示（实时）。</summary>
    public ObservableCollection<CommandValidationIssue> Issues { get; }

    /// <summary>已选目标单位 id（地图联动或下拉选择）。</summary>
    public string? SelectedTargetUnitId
    {
        get => _selectedTargetUnitId;
        set
        {
            if (SetProperty(ref _selectedTargetUnitId, value))
            {
                Revalidate();
            }
        }
    }

    /// <summary>已选需求类型（5 种枚举之一）。</summary>
    public string? SelectedRequestType
    {
        get => _selectedRequestType;
        set
        {
            if (SetProperty(ref _selectedRequestType, value))
            {
                Revalidate();
            }
        }
    }

    /// <summary>数量输入文本（空 = 缺省 1；非法输入内嵌提示）。</summary>
    public string QuantityText
    {
        get => _quantityText;
        set
        {
            if (_quantityText == value)
            {
                return;
            }

            _quantityText = value;
            OnPropertyChanged();
            ParseQuantity(value);
        }
    }

    /// <summary>已解析数量（缺省 1；0/负值由三级校验阻断）。</summary>
    public ulong Quantity => _quantity is > 0 ? (ulong)_quantity.Value : 1;

    /// <summary>受理上级节点输入文本（空 = 核心按场景支援配置缺省）。</summary>
    public string ToNodeText
    {
        get => _toNodeText;
        set
        {
            if (SetProperty(ref _toNodeText, value ?? string.Empty))
            {
                Revalidate();
            }
        }
    }

    /// <summary>关联任务命令 id 输入文本（空 = 不关联，FR-009 缺省）。</summary>
    public string ForCommandIdText
    {
        get => _forCommandIdText;
        set
        {
            if (SetProperty(ref _forCommandIdText, value ?? string.Empty))
            {
                Revalidate();
            }
        }
    }

    /// <summary>优先级输入文本（空 = 0 并触发 SUPPORT_LOW_PRIORITY 警告）。</summary>
    public string PriorityText
    {
        get => _priorityText;
        set
        {
            if (_priorityText == value)
            {
                return;
            }

            _priorityText = value;
            OnPropertyChanged();
            ParsePriority(value);
        }
    }

    /// <summary>时限输入文本（tick；空 = 0 = 未设置并触发时限警告）。</summary>
    public string DeadlineTickText
    {
        get => _deadlineTickText;
        set
        {
            if (_deadlineTickText == value)
            {
                return;
            }

            _deadlineTickText = value;
            OnPropertyChanged();
            ParseDeadlineTick(value);
        }
    }

    /// <summary>连排级剩余支援分数（快照 support 摘要）。</summary>
    public ulong ScoreRemaining => _scoreRemaining;

    /// <summary>本次请求预计扣减分数（quantity × Σ选中种类成本，与 native 同式）。</summary>
    public ulong EstimatedCost
    {
        get
        {
            ulong cost = 0;
            foreach (SupportKindSelectionItem item in KindOptions.Where(item => item.IsSelected))
            {
                // 数量/成本来自玩家输入，乘法可能溢出：饱和到 ulong.MaxValue
                // （必然显示"不足"）而不是向渲染循环抛异常。
                ulong add = item.Option.Cost * Quantity;
                cost = cost > ulong.MaxValue - add ? ulong.MaxValue : cost + add;
            }

            return cost;
        }
    }

    /// <summary>预计扣减是否超过剩余分数（连排级有限分数用尽即止提示）。</summary>
    public bool ScoreInsufficient => EstimatedCost > _scoreRemaining;

    /// <summary>评估中请求数（快照摘要）。</summary>
    public ulong PendingRequests => _pendingRequests;

    /// <summary>在编配属记录数（快照摘要）。</summary>
    public ulong Attaches => _attaches;

    /// <summary>剩余分数展示文案。</summary>
    public string ScoreRemainingText => $"剩余分数：{_scoreRemaining}";

    /// <summary>预计扣减展示文案。</summary>
    public string EstimatedCostText => $"本次预计扣减：{EstimatedCost}";

    /// <summary>扣减后剩余展示文案（不足时明确提示，不静默）。</summary>
    public string ScoreAfterText =>
        ScoreInsufficient
            ? "扣减后剩余：不足（核心将按 INSUFFICIENT_SCORE 拒绝）"
            : $"扣减后剩余：{_scoreRemaining - EstimatedCost}";

    /// <summary>评估中请求展示文案。</summary>
    public string PendingText => $"评估中请求：{_pendingRequests}";

    /// <summary>在编配属展示文案。</summary>
    public string AttachText => $"在编配属：{_attaches}";

    /// <summary>最新请求状态文案（事件映射，SupportStatusMapper）。</summary>
    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    /// <summary>最新请求状态。</summary>
    public SupportRequestStatus Status
    {
        get => _status;
        private set => SetProperty(ref _status, value);
    }

    /// <summary>场景是否配置支援管线。</summary>
    public bool IsSupportConfigured => _options.Configured;

    /// <summary>支援模式提示（连排级有限分数 / 营级配属链 / 未配置）。</summary>
    public string SupportModeHint
    {
        get
        {
            if (!_options.Configured)
            {
                return "当前场景未配置支援（提交可能被核心拒绝）。";
            }

            return _options.Scale == "battalion"
                ? "营级规模：走完整配属链，由上级评估配属/拒绝/转请（不扣分数）。"
                : "连排级规模：有限分数支援（按种类成本扣分，用尽即止）。";
        }
    }

    /// <summary>当前是否允许提交（无阻断错误）。</summary>
    public bool CanSubmit
    {
        get => _canSubmit;
        private set => SetProperty(ref _canSubmit, value);
    }

    /// <summary>选择目标单位（下拉/地图联动）。</summary>
    public IRelayCommand<string?> SelectTargetCommand { get; }

    /// <summary>选择需求类型（单选）。</summary>
    public IRelayCommand<string?> SelectRequestTypeCommand { get; }

    /// <summary>提交支援请求。</summary>
    public IRelayCommand SubmitCommand { get; }

    /// <summary>每帧喂入快照与己方阵营：刷新分数/计数/目标池，并按需查询事件状态。</summary>
    /// <param name="snapshot">只读快照。</param>
    /// <param name="friendlySide">己方阵营标识（与命令上下文同源派生）。</param>
    public void ApplySnapshot(SimulationSnapshot snapshot, string friendlySide)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        _friendlySide = friendlySide ?? string.Empty;
        _commanderNodeId = snapshot.PlayerNodeId;
        _snapshotTick = snapshot.Tick;
        _scoreRemaining = snapshot.Support.ScoreRemaining;
        _pendingRequests = snapshot.Support.PendingRequests;
        _attaches = snapshot.Support.Attaches;
        OnPropertyChanged(nameof(ScoreRemaining));
        OnPropertyChanged(nameof(ScoreRemainingText));
        OnPropertyChanged(nameof(ScoreInsufficient));
        OnPropertyChanged(nameof(ScoreAfterText));
        OnPropertyChanged(nameof(PendingRequests));
        OnPropertyChanged(nameof(PendingText));
        OnPropertyChanged(nameof(Attaches));
        OnPropertyChanged(nameof(AttachText));

        // 目标池：己方可指挥单位（同阵营 + 玩家节点范围），内容不变不重建（F8）。
        var next = new List<CommandableUnit>();
        foreach (UnitState unit in snapshot.Units)
        {
            if (unit.Side != _friendlySide)
            {
                continue;
            }

            if (!string.IsNullOrEmpty(_commanderNodeId) && unit.NodeId != _commanderNodeId)
            {
                continue;
            }

            next.Add(new CommandableUnit(
                unit.Id,
                unit.NodeId,
                unit.Side,
                unit.Ammo.Keys.ToList(),
                unit.MissionActive));
        }

        if (!TargetUnits.SequenceEqual(next))
        {
            TargetUnits.Clear();
            foreach (CommandableUnit unit in next)
            {
                TargetUnits.Add(unit);
            }
        }

        _friendlyUnits = next;
        if (_selectedTargetUnitId is not null && next.All(unit => unit.Id != _selectedTargetUnitId))
        {
            // 目标已不可指挥（消灭/脱离节点）：清除选择，避免提交失效单位。
            _selectedTargetUnitId = null;
            OnPropertyChanged(nameof(SelectedTargetUnitId));
        }

        RefreshStatusFromEvents(snapshot.Tick);
        RecomputeScore();
        Revalidate();
    }

    /// <summary>设置目标单位（地图联动；无效/空白目标清除选择）。</summary>
    /// <param name="unitId">己方可指挥单位 id。</param>
    public void SetTarget(string? unitId)
    {
        if (string.IsNullOrWhiteSpace(unitId) || _friendlyUnits.All(unit => unit.Id != unitId))
        {
            SelectedTargetUnitId = null;
            return;
        }

        SelectedTargetUnitId = unitId;
    }

    /// <summary>切换支援种类勾选态。</summary>
    /// <param name="kindId">资源池种类 id。</param>
    public void ToggleKind(string? kindId)
    {
        SupportKindSelectionItem? item = KindOptions.FirstOrDefault(option => option.Id == kindId);
        if (item is not null)
        {
            item.IsSelected = !item.IsSelected;
        }
    }

    /// <summary>运行三级校验并提交：有错误返回 false；成功序列化并触发提交事件。</summary>
    /// <param name="commandJson">成功时输出命令 JSON；失败为 null。</param>
    /// <returns>是否已提交。</returns>
    public bool TrySubmit(out string? commandJson)
    {
        CommandDraft draft = BuildDraft();
        CommandValidationResult result = CommandValidationRules.Validate(draft, BuildContext());
        bool hasInputErrors = _inputIssues.Values.Any(issue => issue.Severity == CommandIssueSeverity.Error);
        if (result.HasErrors || hasInputErrors)
        {
            commandJson = null;
            return false;
        }

        commandJson = CommandJsonBuilder.Build(draft);
        SubmitRequested?.Invoke(this, commandJson);
        return true;
    }

    /// <summary>核心拒绝命令后回填内嵌错误（核心是最终权威）。</summary>
    /// <param name="exception">核心抛出的异常（携带错误码与原因）。</param>
    public void ShowNativeRejection(SimNativeException exception)
    {
        Issues.Add(new CommandValidationIssue(
            CommandIssueSeverity.Error,
            "NATIVE_REJECTED",
            $"核心拒绝支援请求（{SimNativeException.Describe(exception.ResultCode)}）：{exception.Message}"));
        CanSubmit = false;
        SubmitCommand.NotifyCanExecuteChanged();
    }

    private void SetRequestType(string? requestType)
    {
        SelectedRequestType = string.IsNullOrWhiteSpace(requestType) ? null : requestType;
    }

    private void Submit() => TrySubmit(out _);

    private void ParseQuantity(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            // 空输入 = 缺省 1（schema 允许 quantity 缺省，native 同规则）。
            ClearInputIssue("QUANTITY");
            _quantity = null;
            OnPropertyChanged(nameof(Quantity));
            RecomputeScore();
            Revalidate();
            return;
        }

        if (long.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out long value))
        {
            ClearInputIssue("QUANTITY");
            _quantity = value;
            OnPropertyChanged(nameof(Quantity));
            RecomputeScore();
            Revalidate();
            return;
        }

        SetInputIssue("QUANTITY", $"“{text}”不是有效数量（支援数量须为 ≥ 1 的整数）。");
    }

    private void ParsePriority(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            ClearInputIssue("PRIORITY");
            _priority = 0;
            Revalidate();
            return;
        }

        if (long.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out long value))
        {
            ClearInputIssue("PRIORITY");
            _priority = value;
            Revalidate();
            return;
        }

        SetInputIssue("PRIORITY", $"“{text}”不是有效整数（优先级，支援请求）。");
    }

    private void ParseDeadlineTick(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            ClearInputIssue("DEADLINE_TICK");
            _deadlineTick = 0;
            Revalidate();
            return;
        }

        if (ulong.TryParse(text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out ulong value))
        {
            ClearInputIssue("DEADLINE_TICK");
            _deadlineTick = value;
            Revalidate();
            return;
        }

        SetInputIssue("DEADLINE_TICK", $"“{text}”不是有效非负整数（时限，单位 tick）。");
    }

    private void SetInputIssue(string fieldKey, string message)
    {
        if (_inputIssues.TryGetValue(fieldKey, out CommandValidationIssue? existing) &&
            existing.Message == message)
        {
            return;
        }

        _inputIssues[fieldKey] = new CommandValidationIssue(
            CommandIssueSeverity.Error, "NUMERIC_INPUT_INVALID", message);
        Revalidate();
    }

    private void ClearInputIssue(string fieldKey)
    {
        if (_inputIssues.Remove(fieldKey))
        {
            Revalidate();
        }
    }

    private void RecomputeScore()
    {
        OnPropertyChanged(nameof(EstimatedCost));
        OnPropertyChanged(nameof(EstimatedCostText));
        OnPropertyChanged(nameof(ScoreInsufficient));
        OnPropertyChanged(nameof(ScoreAfterText));
    }

    private void Revalidate()
    {
        Issues.Clear();
        foreach (CommandValidationIssue issue in _inputIssues.Values)
        {
            Issues.Add(issue);
        }

        bool hasInputErrors = _inputIssues.Values.Any(issue => issue.Severity == CommandIssueSeverity.Error);
        if (_friendlySide.Length == 0)
        {
            CanSubmit = false;
            SubmitCommand.NotifyCanExecuteChanged();
            return;
        }

        CommandValidationResult result = CommandValidationRules.Validate(BuildDraft(), BuildContext());
        foreach (CommandValidationIssue issue in result.Issues)
        {
            Issues.Add(issue);
        }

        CanSubmit = !result.HasErrors && !hasInputErrors;
        SubmitCommand.NotifyCanExecuteChanged();
    }

    private CommandDraft BuildDraft()
    {
        var draft = new CommandDraft
        {
            Type = "SUPPORT_REQUEST",
            Condition = "support",
            SupportRequestType = SelectedRequestType,
            SupportQuantity = _quantity,
            SupportToNode = string.IsNullOrWhiteSpace(ToNodeText) ? null : ToNodeText.Trim(),
            SupportForCommandId = string.IsNullOrWhiteSpace(ForCommandIdText) ? null : ForCommandIdText.Trim(),
            Priority = _priority,
            DeadlineTick = _deadlineTick,
        };
        if (SelectedTargetUnitId is not null)
        {
            draft.ExecutorIds.Add(SelectedTargetUnitId);
        }

        foreach (SupportKindSelectionItem item in KindOptions.Where(item => item.IsSelected))
        {
            draft.SupportKinds.Add(item.Id);
        }

        return draft;
    }

    private CommandContext BuildContext() =>
        new()
        {
            CommanderNodeId = _commanderNodeId,
            FriendlySide = _friendlySide,
            CurrentTick = _snapshotTick,
            Units = _friendlyUnits,
            ZoneIds = [],
            SupportPool = _options.AvailableKinds,
            SupportScoreRemaining = _scoreRemaining,
        };

    private void RefreshStatusFromEvents(ulong tick)
    {
        if (_client is null)
        {
            return;
        }

        // 同一 tick 内不重查；tick 前进后按节流间隔查询（与事件日志面板同策略）。
        if (tick == _lastSyncedTick && _timeProvider.GetUtcNow() - _lastStatusPullAt < StatusRefreshThrottle)
        {
            return;
        }

        _lastSyncedTick = tick;
        _lastStatusPullAt = _timeProvider.GetUtcNow();
        try
        {
            var events = new List<SimEventDto>();
            events.AddRange(EventQueryReader.Parse(_client.QueryEvents("{\"text\":\"SUPPORT_\"}")).Events);
            events.AddRange(EventQueryReader.Parse(_client.QueryEvents("{\"text\":\"ATTACH_RETURN\"}")).Events);
            SupportStatusInfo info = SupportStatusMapper.Map(events.OrderBy(entry => entry.Seq).ToList());
            Status = info.Status;
            StatusText = info.StatusText;
        }
        catch (ObjectDisposedException)
        {
            // 返回主菜单/关窗竞态：保留上次状态，不向渲染循环抛异常。
            StatusText = "模拟核心已释放：支援状态查询已停止。";
        }
        catch (Exception exception)
        {
            // 查询失败不打断面板编辑：显示中文错误并保留上次成功状态。
            StatusText = $"查询支援状态失败：{exception.Message}";
        }
    }
}
