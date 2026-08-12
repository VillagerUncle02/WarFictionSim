// 测试：UI↔核心互操作 —— ABI 版本一致性校验（T043）。
//
// C# 桥与 C 核心必须按 wfs_sim_version 对齐（contracts/sim-c-api.md）：
// 错配时显式失败，防止用错误的函数签名/快照结构解析数据（宪法第 17 条）。

using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.Interop;

public class SimAbiVersionTests
{
    [Fact]
    public void ExpectedVersion_MatchesContract()
    {
        // 契约值来自 native/sim/include/wfs/sim/c_api.h 的 WFS_SIM_VERSION_STRING。
        Assert.Equal("0.2.0", SimAbiVersion.Expected);
    }

    [Theory]
    [InlineData("0.2.0", true)]
    [InlineData("0.1.0", false)]
    [InlineData("0.2.1", false)]
    [InlineData(null, false)]
    [InlineData("", false)]
    public void IsCompatible_DecidesByExactMatch(string? actual, bool expected)
    {
        Assert.Equal(expected, SimAbiVersion.IsCompatible(actual));
    }

    [Fact]
    public void Verify_WithMismatch_ThrowsVersionException()
    {
        SimAbiVersionMismatchException exception =
            Assert.Throws<SimAbiVersionMismatchException>(() => SimAbiVersion.Verify("9.9.9"));

        Assert.Equal("0.2.0", exception.Expected);
        Assert.Equal("9.9.9", exception.Actual);
        Assert.Contains("9.9.9", exception.Message, StringComparison.Ordinal);
        Assert.Contains("0.2.0", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Verify_WithCompatibleVersion_DoesNotThrow()
    {
        SimAbiVersion.Verify("0.2.0");
    }
}
