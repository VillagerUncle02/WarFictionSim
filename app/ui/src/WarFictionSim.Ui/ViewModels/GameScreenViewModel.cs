// 文件总览：应用壳 —— 战斗主屏视图模型（T039–T042 集成）。
//
// 职责：把一份快照分发给各面板（地图/命令上下文/事件计数），并把命令面板
// 的提交 JSON 经 ISimClient 注入核心；核心拒绝时回填命令面板错误。
// 表现层帧由视图定时器驱动（渲染独立于模拟 tick，FR-025）；本类型只读快照
// 与注入命令，不直改模拟状态（宪法第 14 条）。

using CommunityToolkit.Mvvm.ComponentModel;
using WarFictionSim.Ui.BattleMap;
using WarFictionSim.Ui.CommandPanel;
using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.GameControls;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.MainMenu;

namespace WarFictionSim.Ui.ViewModels;

/// <summary>战斗主屏（地图 + 命令面板 + 时间控制 + 事件日志）。</summary>
public sealed partial class GameScreenViewModel : ObservableObject, IDisposable
{
    private readonly ISimClient _client;
    private readonly IReadOnlyList<string> _zoneIds;
    private bool _disposed;
    private string _tickDisplay = "tick 0";
    private string _stateHash = string.Empty;
    private string? _statusError;
    private string? _lastStepError;
    private CommandContext? _commandContext;
    private ulong _commandContextTick;

    /// <summary>初始化战斗主屏。</summary>
    /// <param name="client">已创建（并已读档）的模拟客户端。</param>
    /// <param name="scenario">场景静态元数据（地图尺寸/区域列表）。</param>
    public GameScreenViewModel(ISimClient client, ScenarioCatalogEntry scenario)
    {
        _client = client;
        _zoneIds = scenario.Zones;
        ScenarioName = scenario.Name;
        BattleMap = new BattleMapViewModel(scenario.MapWidthKm, scenario.MapHeightKm);
        CommandPanel = new CommandPanelViewModel();
        TimeControls = new TimeControlsViewModel((int)scenario.TickHz);
        EventLog = new EventLogViewModel(_client, scenario.TickHz);
        CommandPanel.SubmitRequested += (_, json) => InjectCommand(json);
        // 命令闭环：地图点选/框选 → 由选中己方单位派生命令面板执行单位（FR-045）。
        BattleMap.UnitSelected += OnBattleMapUnitSelected;
        BattleMap.UnitsSelected += OnBattleMapUnitsSelected;
    }

    /// <summary>场景显示名。</summary>
    public string ScenarioName { get; }

    /// <summary>兵牌地图。</summary>
    public BattleMapViewModel BattleMap { get; }

    /// <summary>命令面板。</summary>
    public CommandPanelViewModel CommandPanel { get; }

    /// <summary>时间控制。</summary>
    public TimeControlsViewModel TimeControls { get; }

    /// <summary>事件日志。</summary>
    public EventLogViewModel EventLog { get; }

    /// <summary>当前游戏 tick 展示。</summary>
    public string TickDisplay
    {
        get => _tickDisplay;
        private set => SetProperty(ref _tickDisplay, value);
    }

    /// <summary>最近一次状态哈希（供 HUD 展示确定性身份）。</summary>
    public string StateHash
    {
        get => _stateHash;
        private set => SetProperty(ref _stateHash, value);
    }

    /// <summary>表现层状态错误（快照/步进失败时展示，不静默）。</summary>
    public string? StatusError
    {
        get => _statusError;
        private set => SetProperty(ref _statusError, value);
    }

    /// <summary>渲染帧：拉取快照并分发给各面板（不步进）。</summary>
    public void OnPresentationFrame()
    {
        try
        {
            ApplySnapshot(_client.GetSnapshot());
            StatusError = _lastStepError;
        }
        catch (SimNativeException exception)
        {
            StatusError = $"读取快照失败：{exception.Message}";
        }
        catch (ObjectDisposedException)
        {
            StatusError = "模拟核心已释放：战斗已结束或正在返回主菜单，请返回主菜单后重新开始。";
        }
        catch (SnapshotParseException exception)
        {
            StatusError = $"快照解析失败：{exception.Message}（核心输出与 UI 契约可能不匹配，请检查版本是否一致）。";
        }
    }

    /// <summary>步进一个 tick（由表现层步进泵按档位调用；暂停时不调用）。</summary>
    public void StepOneTick()
    {
        try
        {
            _client.Step();
            _lastStepError = null;
        }
        catch (SimNativeException exception)
        {
            _lastStepError = $"推进模拟失败：{exception.Message}";
        }
        catch (ObjectDisposedException)
        {
            _lastStepError = "模拟核心已释放，步进已停止。";
        }
    }

    /// <summary>把快照分发给各面板。</summary>
    /// <param name="snapshot">只读快照。</param>
    public void ApplySnapshot(SimulationSnapshot snapshot)
    {
        BattleMap.ApplySnapshot(snapshot);
        CommandContext context = BuildCommandContext(snapshot);
        CommandPanel.ApplyContext(context);
        EventLog.ApplySummary(snapshot.EventLog, snapshot.Tick);
        TickDisplay = $"tick {snapshot.Tick}";
        StateHash = _client.GetStateHash();
    }

    /// <summary>把命令 JSON 注入核心；核心拒绝时回填面板错误。</summary>
    /// <param name="commandJson">已序列化的命令。</param>
    public void InjectCommand(string commandJson)
    {
        try
        {
            _client.InjectCommand(commandJson);
        }
        catch (SimNativeException exception)
        {
            CommandPanel.ShowNativeRejection(exception);
        }
        catch (ObjectDisposedException)
        {
            StatusError = "模拟核心已释放，命令未能注入。";
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        BattleMap.UnitSelected -= OnBattleMapUnitSelected;
        BattleMap.UnitsSelected -= OnBattleMapUnitsSelected;
        _client.Dispose();
    }

    private void OnBattleMapUnitSelected(object? sender, string? unitId) =>
        SyncExecutors(unitId is null ? [] : [unitId]);

    private void OnBattleMapUnitsSelected(object? sender, IReadOnlyList<string> unitIds) =>
        SyncExecutors(unitIds);

    /// <summary>执行单位由选中己方单位派生：敌方/未知 id 一律过滤（只读投影）。</summary>
    /// <param name="unitIds">地图选中的单位 id。</param>
    private void SyncExecutors(IReadOnlyList<string> unitIds)
    {
        var friendlyIds = new List<string>();
        if (_commandContext is not null)
        {
            Dictionary<string, CommandableUnit> unitsById =
                _commandContext.Units.ToDictionary(unit => unit.Id, StringComparer.Ordinal);
            foreach (string unitId in unitIds)
            {
                if (unitsById.TryGetValue(unitId, out CommandableUnit? unit) &&
                    unit.Side == _commandContext.FriendlySide)
                {
                    friendlyIds.Add(unitId);
                }
            }
        }

        CommandPanel.SetExecutors(friendlyIds);
    }

    private CommandContext BuildCommandContext(SimulationSnapshot snapshot)
    {
        // 同一 tick 内状态不变：缓存上下文，避免每帧重建数百个 CommandableUnit（F8）。
        if (_commandContext is not null && _commandContextTick == snapshot.Tick)
        {
            return _commandContext;
        }

        string friendlySide = snapshot.Units.FirstOrDefault(unit => unit.NodeId == snapshot.PlayerNodeId)?.Side
            ?? snapshot.PlayerNodeId;
        var units = new List<CommandableUnit>(snapshot.Units.Count);
        foreach (UnitState unit in snapshot.Units)
        {
            units.Add(new CommandableUnit(
                unit.Id,
                unit.NodeId,
                unit.Side,
                unit.Ammo.Keys.ToList(),
                unit.MissionActive));
        }

        var context = new CommandContext
        {
            CommanderNodeId = snapshot.PlayerNodeId,
            FriendlySide = friendlySide,
            CurrentTick = snapshot.Tick,
            Units = units,
            ZoneIds = _zoneIds,
        };
        _commandContext = context;
        _commandContextTick = snapshot.Tick;
        return context;
    }
}
