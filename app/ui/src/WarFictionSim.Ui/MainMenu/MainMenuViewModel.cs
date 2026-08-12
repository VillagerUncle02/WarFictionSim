// 文件总览：主菜单 —— 规模选择、扮演节点、新游戏/读档流程（T039）。
//
// 职责边界：只解析选择并产出 GameStartRequest，不创建句柄（宪法第 14 条：
// 表现层不直改模拟状态）。读档前先过两道校验——存档头格式（SaveHeaderReader）
// 与存档 ABI 版本（SimAbiVersion）——任一失败只显示内联错误、不进入战斗；
// 运行时 ABI 校验由应用壳创建句柄时再兜底一次（核心为最终权威）。

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.MainMenu;

/// <summary>主菜单视图模型。</summary>
public sealed partial class MainMenuViewModel : ObservableObject
{
    private const int MinThreads = 1;
    private const int MaxThreads = 64;

    private readonly IScenarioCatalog _catalog;
    private CombatScale _selectedScale = CombatScale.Platoon;
    private ScenarioCatalogEntry? _selectedScenario;
    private string? _selectedNodeId;
    private int _threads = 4;
    private string? _errorMessage;

    /// <summary>初始化主菜单。</summary>
    /// <param name="catalog">场景目录。</param>
    public MainMenuViewModel(IScenarioCatalog catalog)
    {
        _catalog = catalog;
        VisibleScenarios = new ObservableCollection<ScenarioCatalogEntry>();
        AvailableNodes = new ObservableCollection<string>();
        LoadIssuesText = string.Join(Environment.NewLine, catalog.LoadIssues);

        SelectScaleCommand = new RelayCommand<CombatScale>(scale => SelectedScale = scale);
        StartNewGameCommand = new RelayCommand(StartNewGame);
        StartTutorialCommand = new RelayCommand(StartTutorial);
        LoadSaveCommand = new RelayCommand<string?>(LoadSave);

        RefreshVisibleScenarios();
    }

    /// <summary>当玩家完成启动选择时触发（应用壳据此创建句柄并导航）。</summary>
    public event EventHandler<GameStartRequestedEventArgs>? GameStartRequested;

    /// <summary>按所选规模过滤后的场景列表。</summary>
    public ObservableCollection<ScenarioCatalogEntry> VisibleScenarios { get; }

    /// <summary>当前场景可扮演的己方指挥节点。</summary>
    public ObservableCollection<string> AvailableNodes { get; }

    /// <summary>目录加载问题（只读展示，不阻断正常条目）。</summary>
    public string LoadIssuesText { get; }

    /// <summary>当前选择的作战规模。</summary>
    public CombatScale SelectedScale
    {
        get => _selectedScale;
        set
        {
            if (SetProperty(ref _selectedScale, value))
            {
                RefreshVisibleScenarios();
            }
        }
    }

    /// <summary>当前选择的场景。</summary>
    public ScenarioCatalogEntry? SelectedScenario
    {
        get => _selectedScenario;
        set
        {
            if (SetProperty(ref _selectedScenario, value))
            {
                OnSelectedScenarioChanged(value);
            }
        }
    }

    /// <summary>当前选择的扮演节点。</summary>
    public string? SelectedNodeId
    {
        get => _selectedNodeId;
        set => SetProperty(ref _selectedNodeId, value);
    }

    /// <summary>当前场景是否由数据固定扮演节点（v1：C ABI 无节点覆盖参数）。</summary>
    public bool IsPlayerNodeFixed =>
        _selectedScenario is not null && !string.IsNullOrEmpty(_selectedScenario.PlayerNodeId);

    /// <summary>是否允许玩家改选扮演节点（固定节点时禁用下拉）。</summary>
    public bool CanChooseNode => !IsPlayerNodeFixed;

    /// <summary>扮演节点选择说明（固定节点说明来源；未固定说明 v1 由场景决定）。</summary>
    public string PlayerNodeNote => _selectedScenario is null
        ? string.Empty
        : IsPlayerNodeFixed
            ? $"v1 扮演节点由场景数据指定：{_selectedScenario.PlayerNodeId}，无法更换。"
            : "v1 版本扮演节点最终由场景数据决定，此选择暂不生效。";

    /// <summary>并行度（1–64；只影响性能，不影响状态哈希）。</summary>
    public int Threads
    {
        get => _threads;
        set => SetProperty(ref _threads, value);
    }

    /// <summary>内联错误提示（为空表示无错误）。</summary>
    public string? ErrorMessage
    {
        get => _errorMessage;
        private set
        {
            if (SetProperty(ref _errorMessage, value))
            {
                OnPropertyChanged(nameof(HasError));
            }
        }
    }

    /// <summary>是否有未消除的错误提示。</summary>
    public bool HasError => !string.IsNullOrEmpty(ErrorMessage);

    /// <summary>切换作战规模。</summary>
    public IRelayCommand<CombatScale> SelectScaleCommand { get; }

    /// <summary>以当前选择开始新游戏。</summary>
    public IRelayCommand StartNewGameCommand { get; }

    /// <summary>直接进入教程场景。</summary>
    public IRelayCommand StartTutorialCommand { get; }

    /// <summary>读取存档（参数为存档路径，需先通过头/ABI 校验）。</summary>
    public IRelayCommand<string?> LoadSaveCommand { get; }

    /// <summary>显示外部错误（应用壳创建句柄/读档失败时回填到主菜单）。</summary>
    /// <param name="message">错误文案。</param>
    public void ShowError(string message) => ErrorMessage = message;

    partial void OnSelectedScenarioChanged(ScenarioCatalogEntry? value);

    partial void OnSelectedScenarioChanged(ScenarioCatalogEntry? value)
    {
        AvailableNodes.Clear();
        if (value is null)
        {
            SelectedNodeId = null;
            OnPropertyChanged(nameof(IsPlayerNodeFixed));
            OnPropertyChanged(nameof(CanChooseNode));
            OnPropertyChanged(nameof(PlayerNodeNote));
            return;
        }

        foreach (string nodeId in value.CommandNodeIds)
        {
            AvailableNodes.Add(nodeId);
        }

        // 固定节点：直接选中场景 player_node_id（v1 不可更换）；否则默认选第一个
        // 可扮演节点供展示（最终仍由场景数据决定）。
        SelectedNodeId = IsPlayerNodeFixed
            ? value.PlayerNodeId
            : AvailableNodes.Count > 0 ? AvailableNodes[0] : null;
        OnPropertyChanged(nameof(IsPlayerNodeFixed));
        OnPropertyChanged(nameof(CanChooseNode));
        OnPropertyChanged(nameof(PlayerNodeNote));
    }

    private void RefreshVisibleScenarios()
    {
        ScenarioCatalogEntry? previous = SelectedScenario;
        VisibleScenarios.Clear();
        foreach (ScenarioCatalogEntry entry in _catalog.Entries.Where(entry => entry.Scale == SelectedScale))
        {
            VisibleScenarios.Add(entry);
        }

        SelectedScenario = previous is not null && VisibleScenarios.Contains(previous)
            ? previous
            : VisibleScenarios.FirstOrDefault();
    }

    private void StartNewGame()
    {
        if (!TryResolveSelection(out ScenarioCatalogEntry? scenario, out string? error))
        {
            ErrorMessage = error;
            return;
        }

        RaiseStart(new GameStartRequest(scenario!, scenario!.Seed, Threads, null));
    }

    private void StartTutorial()
    {
        ScenarioCatalogEntry? tutorial = _catalog.Entries.FirstOrDefault(entry => entry.IsTutorial);
        if (tutorial is null)
        {
            ErrorMessage = "未找到教程场景。";
            return;
        }

        if (!IsValidThreadCount())
        {
            ErrorMessage = $"并行度需在 {MinThreads}–{MaxThreads} 之间。";
            return;
        }

        RaiseStart(new GameStartRequest(tutorial, tutorial.Seed, Threads, null));
    }

    private void LoadSave(string? savePath)
    {
        if (string.IsNullOrWhiteSpace(savePath))
        {
            ErrorMessage = "请选择存档文件。";
            return;
        }

        // 第一道校验：存档文件格式（magic + 格式版本 + 头 JSON），
        // 与核心 save.cpp 的规则一致；失败绝不进入战斗。
        if (!SaveHeaderReader.TryRead(savePath, out SaveHeader? header, out string? readError) || header is null)
        {
            ErrorMessage = readError;
            return;
        }

        // 第二道校验：存档写入时的 ABI 版本必须与当前 UI 期望一致
        // （读档先经 C ABI 校验版本再进入，FR-013 精神/宪法 13）。
        try
        {
            SimAbiVersion.Verify(header.AbiVersion);
        }
        catch (SimAbiVersionMismatchException exception)
        {
            ErrorMessage = $"存档无法读取：{exception.Message}";
            return;
        }

        ScenarioCatalogEntry? scenario = _catalog.FindById(header.ScenarioId);
        if (scenario is null)
        {
            ErrorMessage = $"存档对应的场景未安装：{header.ScenarioId}。";
            return;
        }

        if (!IsValidThreadCount())
        {
            ErrorMessage = $"并行度需在 {MinThreads}–{MaxThreads} 之间。";
            return;
        }

        RaiseStart(new GameStartRequest(scenario, header.Seed, Threads, savePath));
    }

    private bool TryResolveSelection(out ScenarioCatalogEntry? scenario, out string? error)
    {
        scenario = SelectedScenario;
        if (scenario is null)
        {
            error = "请先选择作战规模与场景。";
            return false;
        }

        if (scenario is not null &&
            !string.IsNullOrEmpty(scenario.PlayerNodeId) &&
            SelectedNodeId != scenario.PlayerNodeId)
        {
            error = $"该场景 v1 扮演节点由场景数据指定为 {scenario.PlayerNodeId}，无法更换。";
            return false;
        }

        if (!IsValidThreadCount())
        {
            error = $"并行度需在 {MinThreads}–{MaxThreads} 之间。";
            return false;
        }

        error = null;
        return true;
    }

    private bool IsValidThreadCount() => Threads is >= MinThreads and <= MaxThreads;

    private void RaiseStart(GameStartRequest request)
    {
        ErrorMessage = null;
        GameStartRequested?.Invoke(this, new GameStartRequestedEventArgs(request));
    }
}
