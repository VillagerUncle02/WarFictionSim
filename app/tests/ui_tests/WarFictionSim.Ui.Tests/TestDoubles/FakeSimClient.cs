// 测试替身：实现 ISimClient 的可配置假实现（各面板 ViewModel 单元测试用）。
//
// 为什么存在：ViewModel 只依赖 ISimClient 抽象，测试不需要真实 sim_core.dll；
// 假实现记录注入/步进/存档调用，让断言聚焦表现层逻辑而非原生边界。

using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.Tests.TestDoubles;

public sealed class FakeSimClient : ISimClient
{
    public FakeSimClient(SimulationSnapshot snapshot)
    {
        Snapshot = snapshot;
        AbiVersion = SimAbiVersion.Expected;
        StateHash = new string('0', 64);
    }

    public string AbiVersion { get; set; }

    public SimulationSnapshot Snapshot { get; set; }

    public string StateHash { get; set; }

    public List<string> InjectedCommands { get; } = [];

    public int StepCount { get; private set; }

    public List<string> SavedPaths { get; } = [];

    public List<string> LoadedPaths { get; } = [];

    public Exception? NextGetSnapshotError { get; set; }

    public SimNativeException? NextInjectError { get; set; }

    public bool Disposed { get; private set; }

    public SimulationSnapshot GetSnapshot()
    {
        if (NextGetSnapshotError is not null)
        {
            throw NextGetSnapshotError;
        }

        return Snapshot;
    }

    public string GetStateHash() => StateHash;

    public void Step() => StepCount++;

    public void InjectCommand(string commandJson)
    {
        if (NextInjectError is not null)
        {
            throw NextInjectError;
        }

        InjectedCommands.Add(commandJson);
    }

    public void Save(string path) => SavedPaths.Add(path);

    public void LoadSave(string path) => LoadedPaths.Add(path);

    public void Dispose() => Disposed = true;
}
