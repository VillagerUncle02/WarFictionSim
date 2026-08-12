// 测试：UI↔核心互操作 —— 连排级闭环端到端验证（T044，US1 收尾）。
//
// 验证目标（quickstart §3.1–3.3 的连排级等价物，scn-tutorial-platoon.json）：
// - 同输入哈希一致：真实 SimNativeBridge（sim_core.dll）与 sim_headless.exe
//   以相同 scenario/seed/命令脚本推进后，state hash 逐字节一致
//   （UI 桥与无头核心同源确定性，宪法第 7/14/15 条）；
// - 命令链路：COMMAND_ISSUED → COMMAND_ACKNOWLEDGED → UNIT_MOVING →
//   MISSION_COMPLETED，连排级通讯延迟 3–10s（20 Hz 下 60–200 tick）；
// - 战斗结算：COMBAT_HIT / COMBAT_AREA_HIT 在两条路径都出现；
// - 同输入两次运行哈希一致（SC-001 连排级等价）。
//
// 无头等价路径说明：sim_headless inject 子命令先逐行注入脚本全部命令
// （此时 clock 处于 tick 0），再统一推进 --ticks 个 tick（见
// native/sim/src/headless_driver.cpp）。因此 UI 桥路径同样在 tick 0 注入
// 同一条命令 JSON，随后推进相同 tick 数，两条路径的操作序列严格对齐，
// state hash 才具备逐字节可比性。命令 JSON 用生产代码 CommandJsonBuilder
// 构造（与命令面板同一序列化器），保证"走与命令面板相同的 JSON 结构"。

using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using WarFictionSim.Ui.CommandPanel;
using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.Interop;

public class HeadlessParityE2ETests
{
    private const ulong Seed = 42;
    private const int Threads = 1;
    private const int TotalTicks = 14000;
    private const double MoveTargetX = 0.95;
    private const double MoveTargetY = 0.95;
    private const ulong CommandDeadlineTick = 20000;
    private const string ScenarioFileName = "scn-tutorial-platoon.json";
    private const string ExecutorId = "tutorial-squad-1";
    // 无头 CLI 等待上限：核心回归导致卡死时测试必须显式失败而非无限挂起
    // （复审 us1-e2e-r1.md 🟡）。
    private const int HeadlessWaitTimeoutSeconds = 60;

    [Fact]
    public async Task UiBridgeAndHeadless_ProduceByteIdenticalStateHash()
    {
        using TestEnvironment environment = PrepareEnvironment();
        string commandJson = BuildMoveCommand();

        UiRun ui = RunUiBridge(environment.ScenarioPath, commandJson);
        HeadlessRun headless = await RunHeadlessAsync(environment, commandJson);

        AssertStateHash(ui.StateHash);
        AssertStateHash(headless.StateHash);

        // 核心断言：UI 桥与无头 CLI 的最终状态哈希逐字节一致。
        Assert.Equal(headless.StateHash, ui.StateHash);

        // 闭环覆盖：命令链路与战斗结算在两条路径都完整出现。
        AssertCommandChain(ui.Messages);
        AssertCommandChain(headless.Messages);
        AssertCombatSettlement(ui.Messages);
        AssertCombatSettlement(headless.Messages);

        // UI 的 wfs_sim_query_events 与无头 JSONL 输出同一事件序列。
        Assert.Equal(headless.Messages, ui.Messages);
    }

    [Fact]
    public async Task SameInput_RerunTwice_YieldsSameHashAndEventSequence()
    {
        using TestEnvironment environment = PrepareEnvironment();
        string commandJson = BuildMoveCommand();

        UiRun uiFirst = RunUiBridge(environment.ScenarioPath, commandJson);
        UiRun uiSecond = RunUiBridge(environment.ScenarioPath, commandJson);
        HeadlessRun headlessFirst = await RunHeadlessAsync(environment, commandJson);
        HeadlessRun headlessSecond = await RunHeadlessAsync(environment, commandJson);

        // SC-001 连排级等价：同输入两次运行哈希一致。
        Assert.Equal(headlessFirst.StateHash, headlessSecond.StateHash);
        Assert.Equal(uiFirst.StateHash, uiSecond.StateHash);

        // 确定性不只覆盖哈希，也覆盖事件序列（宪法第 7 条）。
        Assert.Equal(headlessFirst.Messages, headlessSecond.Messages);
        Assert.Equal(uiFirst.Messages, uiSecond.Messages);

        // 两次运行都与另一条路径保持逐字节一致（跨形态一致性）。
        Assert.Equal(headlessFirst.StateHash, uiFirst.StateHash);
    }

    /// <summary>按命令面板同一序列化器构造一条合法 MOVE 命令。</summary>
    private static string BuildMoveCommand()
    {
        var draft = new CommandDraft
        {
            Type = "MOVE",
            Condition = "reach_point",
            Intent = "E2E：squad-1 前出接敌",
            Engagement = "aggressive",
            FailureAction = "report",
            Priority = 0,
            DeadlineTick = CommandDeadlineTick,
            PointX = MoveTargetX,
            PointY = MoveTargetY,
        };
        draft.ExecutorIds.Add(ExecutorId);
        return CommandJsonBuilder.Build(draft);
    }

    /// <summary>用真实 SimNativeBridge 走"创建 → tick 0 注入 → 推进 → 哈希"路径。</summary>
    private static UiRun RunUiBridge(string scenarioPath, string commandJson)
    {
        using ISimClient client = SimNativeBridge.Create(scenarioPath, Seed, Threads);

        // 命令注入是 UI 修改模拟状态的唯一写路径（宪法第 14 条）。
        client.InjectCommand(commandJson);
        for (int tick = 0; tick < TotalTicks; tick++)
        {
            client.Step();
        }

        string stateHash = client.GetStateHash();
        EventQueryResponse events = EventQueryReader.Parse(client.QueryEvents("{}"));
        return new UiRun(stateHash, events.Events.Select(entry => entry.Message).ToArray());
    }

    /// <summary>驱动 sim_headless inject 跑同一 scenario/seed/命令脚本（带超时与管道防死锁）。</summary>
    private static async Task<HeadlessRun> RunHeadlessAsync(TestEnvironment environment, string commandJson)
    {
        string scriptPath = Path.Combine(environment.TempDir, $"e2e-move-{Guid.NewGuid():N}.jsonl");
        string eventsPath = Path.Combine(environment.TempDir, $"e2e-events-{Guid.NewGuid():N}.jsonl");
        File.WriteAllText(scriptPath, commandJson + Environment.NewLine, new UTF8Encoding(false));

        var startInfo = new ProcessStartInfo
        {
            FileName = environment.HeadlessExe,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("inject");
        startInfo.ArgumentList.Add("--scenario");
        startInfo.ArgumentList.Add(environment.ScenarioPath);
        startInfo.ArgumentList.Add("--seed");
        startInfo.ArgumentList.Add(Seed.ToString(CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("--threads");
        startInfo.ArgumentList.Add(Threads.ToString(CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("--ticks");
        startInfo.ArgumentList.Add(TotalTicks.ToString(CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("--script");
        startInfo.ArgumentList.Add(scriptPath);
        startInfo.ArgumentList.Add("--out");
        startInfo.ArgumentList.Add(eventsPath);
        startInfo.ArgumentList.Add("--hash");

        using Process process = Process.Start(startInfo)
            ?? throw new InvalidOperationException($"无法启动无头 CLI：{environment.HeadlessExe}");

        // 两个流先并发消费（避免 stdout/stderr 单边读满缓冲区造成互相等待的
        // 管道死锁），再按超时上限等待退出；超时则终止整棵进程树并显式失败
        // （宪法第 17 条：不静默吞错，也不无限挂起）。
        Task<string> standardOutputTask = process.StandardOutput.ReadToEndAsync();
        Task<string> standardErrorTask = process.StandardError.ReadToEndAsync();
        bool exitedInTime;
        try
        {
            // Process.WaitForExitAsync 无 TimeSpan 重载，用 Task.WaitAsync 施加
            // 超时上限；TimeoutException 仅可能由本次等待抛出。
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(HeadlessWaitTimeoutSeconds));
            exitedInTime = true;
        }
        catch (TimeoutException)
        {
            exitedInTime = false;
        }

        if (!exitedInTime)
        {
            int processId = process.Id;
            try
            {
                process.Kill(entireProcessTree: true);
            }
            catch (InvalidOperationException)
            {
                // 竞态兜底：进程恰在超时判定后自行退出，Kill 因进程已退出而抛
                // InvalidOperationException；此时继续读关闭后的流并走超时失败路径。
            }

            string timedOutOutput = await standardOutputTask;
            string timedOutError = await standardErrorTask;
            throw new InvalidOperationException(
                $"sim_headless inject 超时（{HeadlessWaitTimeoutSeconds} 秒未退出），已终止进程树。\n" +
                $"可执行文件：{environment.HeadlessExe}\n进程 Id：{processId}\n" +
                $"标准输出：{timedOutOutput.Trim()}\n标准错误：{timedOutError.Trim()}");
        }

        string standardOutput = await standardOutputTask;
        string standardError = await standardErrorTask;

        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"sim_headless inject 运行失败（退出码 {process.ExitCode}）。\n标准错误：{standardError.Trim()}");
        }

        string hash = standardOutput
            .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .LastOrDefault()?
            .Trim() ?? string.Empty;
        if (!Regex.IsMatch(hash, "^[0-9a-f]{64}$"))
        {
            throw new InvalidOperationException(
                $"sim_headless inject 未输出合法的 64 位小写十六进制哈希。\n标准输出：{standardOutput.Trim()}");
        }

        return new HeadlessRun(hash, ReadEventMessages(eventsPath));
    }

    /// <summary>读取无头 JSONL 事件输出的 message 序列（按文件顺序，即 seq 升序）。</summary>
    private static IReadOnlyList<string> ReadEventMessages(string path)
    {
        if (!File.Exists(path))
        {
            throw new InvalidOperationException($"sim_headless 未生成事件 JSONL 输出：{path}");
        }

        var messages = new List<string>();
        foreach (string line in File.ReadLines(path))
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            try
            {
                using JsonDocument document = JsonDocument.Parse(line);
                messages.Add(document.RootElement.GetProperty("message").GetString() ?? string.Empty);
            }
            catch (JsonException exception)
            {
                throw new InvalidOperationException($"事件 JSONL 存在非法行（{path}）：{line}", exception);
            }
            catch (KeyNotFoundException exception)
            {
                throw new InvalidOperationException($"事件 JSONL 行缺少 message 字段（{path}）：{line}", exception);
            }
        }

        return messages;
    }

    private static void AssertStateHash(string hash)
    {
        Assert.Matches("^[0-9a-f]{64}$", hash);
    }

    private static void AssertCommandChain(IReadOnlyList<string> messages)
    {
        int issued = IndexOfPrefix(messages, "COMMAND_ISSUED command=cmd-0 unit=tutorial-squad-1");
        int acknowledged = IndexOfPrefix(messages, "COMMAND_ACKNOWLEDGED command=cmd-0 unit=tutorial-squad-1");
        int moving = IndexOfPrefix(messages, "UNIT_MOVING unit=tutorial-squad-1");
        int completed = IndexOfPrefix(
            messages, "MISSION_COMPLETED unit=tutorial-squad-1 command=cmd-0 type=MOVE reason=REACH_POINT");

        Assert.True(issued >= 0, "缺少下达事件 COMMAND_ISSUED（命令链路）。");
        Assert.True(acknowledged > issued, "缺少确认事件 COMMAND_ACKNOWLEDGED 或事件顺序错误。");
        Assert.True(moving > acknowledged, "缺少移动事件 UNIT_MOVING 或事件顺序错误。");
        Assert.True(completed > moving, "缺少完成事件 MISSION_COMPLETED 或事件顺序错误。");

        // 连排级通讯延迟 3–10s（20 Hz 下 60–200 tick，quickstart §3.2）。
        Match delayMatch = Regex.Match(messages[acknowledged], @"delay_ticks=(\d+)");
        Assert.True(delayMatch.Success, $"确认事件缺少 delay_ticks 字段：{messages[acknowledged]}");
        int delay = int.Parse(delayMatch.Groups[1].Value, CultureInfo.InvariantCulture);
        Assert.InRange(delay, 60, 200);
    }

    private static void AssertCombatSettlement(IReadOnlyList<string> messages)
    {
        Assert.Contains(messages, message => message.StartsWith("COMBAT_HIT ", StringComparison.Ordinal));
        Assert.Contains(messages, message => message.StartsWith("COMBAT_AREA_HIT ", StringComparison.Ordinal));
    }

    private static int IndexOfPrefix(IReadOnlyList<string> messages, string prefix)
    {
        for (int index = 0; index < messages.Count; index++)
        {
            if (messages[index].StartsWith(prefix, StringComparison.Ordinal))
            {
                return index;
            }
        }

        return -1;
    }

    private static TestEnvironment PrepareEnvironment()
    {
        string repoRoot = FindRepoRoot();
        string scenarioPath = Path.Combine(repoRoot, "data", "scenarios", ScenarioFileName);
        if (!File.Exists(scenarioPath))
        {
            throw new InvalidOperationException($"缺少连排级教程场景：{scenarioPath}");
        }

        RequireSimCoreDll();
        string headlessExe = LocateHeadlessExe(repoRoot);

        string tempDir = Path.Combine(Path.GetTempPath(), "wfs-e2e-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        return new TestEnvironment(scenarioPath, headlessExe, tempDir);
    }

    private static string FindRepoRoot()
    {
        for (DirectoryInfo? directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "data", "scenarios", ScenarioFileName)) &&
                File.Exists(Path.Combine(directory.FullName, "contracts", "schemas", "command.schema.json")))
            {
                return directory.FullName;
            }
        }

        throw new InvalidOperationException(
            $"无法定位仓库根目录：从 {AppContext.BaseDirectory} 逐级向上未找到 " +
            $"data/scenarios/{ScenarioFileName} 与 contracts/schemas/command.schema.json。请从仓库检出内运行测试。");
    }

    private static void RequireSimCoreDll()
    {
        string expectedPath = Path.Combine(AppContext.BaseDirectory, NativeSimLibrary.DefaultLibraryName);
        if (File.Exists(expectedPath))
        {
            return;
        }

        throw new InvalidOperationException(
            $"端到端测试缺少原生核心库 {NativeSimLibrary.DefaultLibraryName}（期望位置：{expectedPath}）。\n" +
            "请先在 native 目录执行 cmake --preset clang-cl-release 与 cmake --build --preset clang-cl-release，" +
            "再重新构建测试工程（构建流程会把 sim_core.dll 复制到测试输出目录）。");
    }

    private static string LocateHeadlessExe(string repoRoot)
    {
        string[] candidates =
        {
            Path.Combine(AppContext.BaseDirectory, "sim_headless.exe"),
            Path.Combine(repoRoot, "native", "build", "clang-cl-release", "sim", "sim_headless.exe"),
            Path.Combine(repoRoot, "native", "build", "clang-cl-release", "sim_headless.exe"),
        };
        foreach (string candidate in candidates)
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        throw new InvalidOperationException(
            "端到端测试缺少无头 CLI sim_headless.exe。\n" +
            $"已检查：{string.Join("；", candidates)}\n" +
            "请先在 native 目录执行 cmake --preset clang-cl-release 与 cmake --build --preset clang-cl-release；" +
            "如依赖测试输出目录内的副本，请重新构建测试工程以触发 exe 复制。");
    }

    private sealed record UiRun(string StateHash, IReadOnlyList<string> Messages);

    private sealed record HeadlessRun(string StateHash, IReadOnlyList<string> Messages);

    private sealed class TestEnvironment : IDisposable
    {
        public TestEnvironment(string scenarioPath, string headlessExe, string tempDir)
        {
            ScenarioPath = scenarioPath;
            HeadlessExe = headlessExe;
            TempDir = tempDir;
        }

        public string ScenarioPath { get; }

        public string HeadlessExe { get; }

        public string TempDir { get; }

        public void Dispose()
        {
            if (Directory.Exists(TempDir))
            {
                Directory.Delete(TempDir, recursive: true);
            }
        }
    }
}
