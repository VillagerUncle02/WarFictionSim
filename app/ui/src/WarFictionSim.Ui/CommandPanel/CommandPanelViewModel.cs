// 文件总览：命令面板 —— 视图模型（T041）。
//
// 流程：点选/框选执行单位 → 选类型（自动带出默认完成条件）→ 填完成条件
// 参数/优先级/时限 → 三级校验（错误阻断提交、警告/建议内嵌展示）→ 序列化
// JSON → 提交事件交给应用壳注入核心；核心拒绝（最终权威）回填为错误条目。
// 全键盘可操作由视图的 Tab 顺序、焦点可见样式与访问键保证。

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>命令面板视图模型。</summary>
public sealed partial class CommandPanelViewModel : ObservableObject
{
    private CommandContext? _context;
    private bool _canSubmit;

    /// <summary>初始化命令面板。</summary>
    public CommandPanelViewModel()
    {
        Draft = new CommandDraft();
        Issues = new ObservableCollection<CommandValidationIssue>();
        ContextUnits = new ObservableCollection<CommandableUnit>();
        ContextZones = new ObservableCollection<string>();
        SelectExecutorCommand = new RelayCommand<string?>(SelectExecutor);
        SetTypeCommand = new RelayCommand<string?>(SetType);
        SubmitCommand = new RelayCommand(Submit, () => CanSubmit);

        // 草稿任何字段变化都触发重校验（三级提示实时内嵌，不打断操作）。
        Draft.PropertyChanged += (_, _) => Revalidate();
        // 执行单位集合增删（点选/框选派生）同样触发重校验，不靠每帧 ApplyContext 掩盖。
        Draft.ExecutorIds.CollectionChanged += (_, _) => Revalidate();
    }

    /// <summary>校验通过并序列化成功时触发（参数为命令 JSON，由应用壳注入核心）。</summary>
    public event EventHandler<string>? SubmitRequested;

    /// <summary>正在编辑的命令草稿。</summary>
    public CommandDraft Draft { get; }

    /// <summary>内嵌三级校验提示（实时）。</summary>
    public ObservableCollection<CommandValidationIssue> Issues { get; }

    /// <summary>上下文中的单位（供目标单位下拉）。</summary>
    public ObservableCollection<CommandableUnit> ContextUnits { get; }

    /// <summary>上下文中的区域（供区域目标下拉）。</summary>
    public ObservableCollection<string> ContextZones { get; }

    /// <summary>可选任务类型目录。</summary>
    public IReadOnlyList<CommandTypeOption> CommandTypes => CommandTypeCatalog.All;

    /// <summary>当前是否允许提交（无阻断错误）。</summary>
    public bool CanSubmit
    {
        get => _canSubmit;
        private set => SetProperty(ref _canSubmit, value);
    }

    /// <summary>点选/框选执行单位（重复点选取消）。</summary>
    public IRelayCommand<string?> SelectExecutorCommand { get; }

    /// <summary>设置命令类型（自动带出默认完成条件）。</summary>
    public IRelayCommand<string?> SetTypeCommand { get; }

    /// <summary>提交命令。</summary>
    public IRelayCommand SubmitCommand { get; }

    /// <summary>应用校验上下文（进入战斗时由应用壳用快照 + 场景元数据合成）。</summary>
    /// <param name="context">校验上下文。</param>
    public void ApplyContext(CommandContext context)
    {
        bool contextChanged = _context is null ||
            context.CurrentTick != _context.CurrentTick ||
            context.CommanderNodeId != _context.CommanderNodeId ||
            !ContextUnits.SequenceEqual(context.Units) ||
            !ContextZones.SequenceEqual(context.ZoneIds);
        _context = context;
        // 仅在内容变化时重建集合：避免每帧 Clear+Add 数百个单位与下拉重绑（F8）。
        if (!ContextUnits.SequenceEqual(context.Units))
        {
            ContextUnits.Clear();
            foreach (CommandableUnit unit in context.Units)
            {
                ContextUnits.Add(unit);
            }
        }

        if (!ContextZones.SequenceEqual(context.ZoneIds))
        {
            ContextZones.Clear();
            foreach (string zoneId in context.ZoneIds)
            {
                ContextZones.Add(zoneId);
            }
        }

        if (contextChanged)
        {
            Revalidate();
        }
    }

    /// <summary>把执行单位集合替换为地图点选/框选派生的结果（内容不变则不重校验）。</summary>
    /// <param name="unitIds">新的执行单位 id 列表（由选中己方单位派生）。</param>
    public void SetExecutors(IEnumerable<string> unitIds)
    {
        List<string> next = unitIds.ToList();
        if (Draft.ExecutorIds.SequenceEqual(next))
        {
            return;
        }

        Draft.ExecutorIds.Clear();
        foreach (string unitId in next)
        {
            Draft.ExecutorIds.Add(unitId);
        }
    }

    /// <summary>切换执行单位选中态。</summary>
    /// <param name="unitId">单位 id。</param>
    public void SelectExecutor(string? unitId)
    {
        if (string.IsNullOrWhiteSpace(unitId))
        {
            return;
        }

        if (Draft.ExecutorIds.Contains(unitId))
        {
            Draft.ExecutorIds.Remove(unitId);
        }
        else
        {
            Draft.ExecutorIds.Add(unitId);
        }
    }

    /// <summary>设置命令类型并带出默认完成条件。</summary>
    /// <param name="typeKey">命令类型键。</param>
    public void SetType(string? typeKey)
    {
        Draft.Type = typeKey;
        Draft.Condition = CommandTypeCatalog.DefaultConditionFor(typeKey);
    }

    /// <summary>设置区域目标。</summary>
    /// <param name="zoneId">区域 id。</param>
    public void SetZoneTarget(string? zoneId) => Draft.ZoneId = zoneId;

    /// <summary>设置单位目标（destroy_unit）。</summary>
    /// <param name="unitId">单位 id。</param>
    public void SetUnitTarget(string? unitId) => Draft.TargetUnitId = unitId;

    /// <summary>设置坐标目标点（reach_point/recon）。</summary>
    /// <param name="x">横坐标（km）。</param>
    /// <param name="y">纵坐标（km）。</param>
    public void SetPointTarget(double? x, double? y)
    {
        Draft.PointX = x;
        Draft.PointY = y;
    }

    /// <summary>运行三级校验（供测试与调试直接检查）。</summary>
    /// <returns>校验结果。</returns>
    public CommandValidationResult Validate() =>
        _context is null
            ? new CommandValidationResult([])
            : CommandValidationRules.Validate(Draft, _context);

    /// <summary>校验并提交：有错误返回 false；成功序列化并触发提交事件。</summary>
    /// <param name="commandJson">成功时输出命令 JSON；失败为 null。</param>
    /// <returns>是否已提交。</returns>
    public bool TrySubmit(out string? commandJson)
    {
        CommandValidationResult result = Validate();
        if (result.HasErrors)
        {
            commandJson = null;
            return false;
        }

        commandJson = CommandJsonBuilder.Build(Draft);
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
            $"核心拒绝命令（{SimNativeException.Describe(exception.ResultCode)}）：{exception.Message}"));
        CanSubmit = false;
        SubmitCommand.NotifyCanExecuteChanged();
    }

    private void Submit() => TrySubmit(out _);

    private void Revalidate()
    {
        Issues.Clear();
        if (_context is null)
        {
            CanSubmit = false;
            SubmitCommand.NotifyCanExecuteChanged();
            return;
        }

        CommandValidationResult result = CommandValidationRules.Validate(Draft, _context);
        foreach (CommandValidationIssue issue in result.Issues)
        {
            Issues.Add(issue);
        }

        CanSubmit = !result.HasErrors;
        SubmitCommand.NotifyCanExecuteChanged();
    }
}
