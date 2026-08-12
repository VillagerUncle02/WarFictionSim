// 测试替身：ISimClientFactory 的可编程假实现（主菜单流程测试用）。
//
// 记录工厂收到的场景路径/种子/线程数，并返回预设客户端，验证
// MainMenuViewModel 只在通过 ABI/存档校验后才真正创建句柄。

using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.Tests.TestDoubles;

public sealed class FakeSimClientFactory : ISimClientFactory
{
    private readonly Func<string, ulong, int, ISimClient> _builder;

    public FakeSimClientFactory(Func<string, ulong, int, ISimClient> builder) => _builder = builder;

    public List<(string Path, ulong Seed, int Threads)> CreateCalls { get; } = [];

    public ISimClient Create(string scenarioPath, ulong seed, int threads)
    {
        CreateCalls.Add((scenarioPath, seed, threads));
        return _builder(scenarioPath, seed, threads);
    }
}
