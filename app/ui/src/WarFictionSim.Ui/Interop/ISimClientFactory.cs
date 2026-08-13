// 文件总览：UI↔核心互操作 —— 模拟客户端工厂抽象（T043）。
//
// 为什么需要工厂：主菜单"新游戏/读档"只是准备启动参数，真正创建句柄在
// 进入战斗时由应用壳完成；工厂抽象让主菜单流程可用假实现测试，
// 并把"创建失败"的错误呈现与导航逻辑解耦。

namespace WarFictionSim.Ui.Interop;

/// <summary>按场景创建模拟客户端。</summary>
public interface ISimClientFactory
{
    /// <summary>创建并加载场景句柄（含 ABI 版本校验）。</summary>
    /// <param name="scenarioPath">场景 JSON 路径。</param>
    /// <param name="seed">显式随机种子。</param>
    /// <param name="threads">并行度（只影响性能，不影响状态哈希）。</param>
    /// <returns>就绪的模拟客户端。</returns>
    /// <exception cref="SimNativeException">场景加载失败或 ABI 错配。</exception>
    ISimClient Create(string scenarioPath, ulong seed, int threads);
}

/// <summary>生产实现：直接创建 <see cref="SimNativeBridge"/>。</summary>
public sealed class SimNativeClientFactory : ISimClientFactory
{
    /// <inheritdoc />
    public ISimClient Create(string scenarioPath, ulong seed, int threads) =>
        SimNativeBridge.Create(scenarioPath, seed, threads);
}
