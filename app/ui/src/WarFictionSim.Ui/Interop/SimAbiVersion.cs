// 文件总览：UI↔核心互操作 —— ABI 版本一致性校验（T043）。
//
// 为什么存在：wfs_sim_version 是 C# 桥与 C++ 核心之间的版本契约
// （contracts/sim-c-api.md）。错配意味着函数签名或快照字段可能已变，
// 此时必须显式拒绝启动/读档，绝不能拿错误结构去解析状态（宪法第 17 条）。

namespace WarFictionSim.Ui.Interop;

/// <summary>ABI 版本契约：期望值与校验入口。</summary>
public static class SimAbiVersion
{
    /// <summary>期望的 ABI 版本（镜像 c_api.h 的 WFS_SIM_VERSION_STRING）。</summary>
    public const string Expected = "0.2.0";

    /// <summary>判断运行时 ABI 版本是否与本 UI 兼容（精确匹配）。</summary>
    /// <param name="actual">wfs_sim_version 返回的版本字符串。</param>
    /// <returns>完全一致返回 <see langword="true"/>。</returns>
    public static bool IsCompatible(string? actual) =>
        string.Equals(actual, Expected, StringComparison.Ordinal);

    /// <summary>校验 ABI 版本，错配时抛出可操作的异常。</summary>
    /// <param name="actual">wfs_sim_version 返回的版本字符串。</param>
    /// <exception cref="SimAbiVersionMismatchException">版本与期望值不一致。</exception>
    public static void Verify(string? actual)
    {
        if (!IsCompatible(actual))
        {
            throw new SimAbiVersionMismatchException(Expected, actual ?? "<null>");
        }
    }
}

/// <summary>模拟核心 ABI 版本与 UI 期望值不一致。</summary>
public sealed class SimAbiVersionMismatchException : Exception
{
    /// <summary>初始化异常。</summary>
    /// <param name="expected">UI 期望的版本。</param>
    /// <param name="actual">核心返回的版本。</param>
    public SimAbiVersionMismatchException(string expected, string actual)
        : base($"模拟核心 ABI 版本不匹配：期望 {expected}，实际 {actual}。" +
               "请重新构建 native 核心与 UI，确保两者来自同一提交；拒绝跨版本运行以防状态被错误解析。")
    {
        Expected = expected;
        Actual = actual;
    }

    /// <summary>UI 期望的版本。</summary>
    public string Expected { get; }

    /// <summary>核心返回的实际版本。</summary>
    public string Actual { get; }
}
