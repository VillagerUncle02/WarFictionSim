// 测试：UI↔核心互操作 —— 两段式文本缓冲读取（T043 + FR-044 数据通道）。
//
// 把"先探长度、按需分配、重试上限、首探成功也要解析"的缓冲协议从 P/Invoke
// 中剥离为纯函数：用假读取回调即可穷举 BUFFER_TOO_SMALL/成功/错误路径，
// 不依赖真实 sim_core.dll。

using System.Text;
using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.Interop;

public class NativeBufferReaderTests
{
    private static int Ok(byte[] buffer, nuint size, out nuint length, string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        Array.Copy(bytes, buffer, bytes.Length);
        length = (nuint)bytes.Length;
        return (int)SimResultCode.Ok;
    }

    [Fact]
    public void FirstProbeSucceeds_ReturnsProbeContent()
    {
        // 修复：首探（4096B）一次成功时不能丢弃真实内容。
        string json = """{"events":[],"count":0,"truncated":false}""";

        string result = NativeBufferReader.Read(
            (byte[] buffer, nuint size, out nuint length) => Ok(buffer, size, out length, json),
            "查询事件日志");

        Assert.Equal(json, result);
    }

    [Fact]
    public void BufferTooSmallThenOk_RetriesWithRequiredSize()
    {
        string json = new string('x', 10_000);
        int calls = 0;

        string result = NativeBufferReader.Read(
            (byte[] buffer, nuint size, out nuint length) =>
            {
                calls++;
                if (calls == 1)
                {
                    length = (nuint)(json.Length + 1); // 含 NUL 的需求字节数。
                    return (int)SimResultCode.BufferTooSmall;
                }

                return Ok(buffer, size, out length, json);
            },
            "读取快照");

        Assert.Equal(json, result);
        Assert.Equal(2, calls);
    }

    [Fact]
    public void BufferRequirementGrowingEveryAttempt_ThrowsInternalError()
    {
        Assert.Throws<SimNativeException>(() =>
            NativeBufferReader.Read(
                (byte[] buffer, nuint size, out nuint length) =>
                {
                    length = (nuint)(size + 1);
                    return (int)SimResultCode.BufferTooSmall;
                },
                "读取快照"));
    }

    [Fact]
    public void NonOkCode_IsMappedToSimNativeException()
    {
        SimNativeException exception = Assert.Throws<SimNativeException>(() =>
            NativeBufferReader.Read(
                (byte[] buffer, nuint size, out nuint length) =>
                {
                    length = 0;
                    return (int)SimResultCode.InvalidData;
                },
                "查询事件日志"));

        Assert.Equal(SimResultCode.InvalidData, exception.ResultCode);
        Assert.Contains("查询事件日志", exception.Message, StringComparison.Ordinal);
    }
}
