// 文件总览：UI↔核心互操作 —— 两段式文本缓冲读取协议（T043 + FR-044 数据通道）。
//
// 为什么独立成纯函数：快照与事件查询共用同一协议——先探测所需大小
// （BUFFER_TOO_SMALL 时 out_len 含 NUL）、再按需分配精确重试（上限 3 次）、
// 首探成功也按实际写出长度解析。从 P/Invoke 中剥离后可用假回调穷举测试，
// 不依赖真实 sim_core.dll（contracts/sim-c-api.md：缓冲生命周期归调用方）。

using System.Text;

namespace WarFictionSim.Ui.Interop;

/// <summary>按 C ABI 两段式缓冲协议读取 UTF-8 文本（快照/事件查询共用）。</summary>
public static class NativeBufferReader
{
    /// <summary>首探缓冲大小（字节，与 P/Invoke 探测路径一致）。</summary>
    public const int InitialBufferSize = 4096;

    /// <summary>缓冲需求连续增长时的最大精确重试次数。</summary>
    public const int MaxAttempts = 3;

    /// <summary>原生读取回调：返回 wfs_sim_result，outLength 为写出或所需字节数。</summary>
    /// <param name="buffer">写入缓冲。</param>
    /// <param name="bufferSize">缓冲大小（字节）。</param>
    /// <param name="outLength">OK 时写出字节数（不含 NUL）；BUFFER_TOO_SMALL 时所需字节数（含 NUL）。</param>
    /// <returns>wfs_sim_result 原始错误码。</returns>
    public delegate int ReadFunc(byte[] buffer, nuint bufferSize, out nuint outLength);

    /// <summary>先探长度、按需重试地读取一份 UTF-8 文本。</summary>
    /// <param name="read">原生读取回调（在调用方锁内串行执行）。</param>
    /// <param name="operation">操作中文名（用于错误文案）。</param>
    /// <returns>去尾 NUL 的 UTF-8 文本。</returns>
    /// <exception cref="SimNativeException">非 OK 错误码或缓冲需求连续增长。</exception>
    public static string Read(ReadFunc read, string operation)
    {
        ArgumentNullException.ThrowIfNull(read);

        nuint required = 0;
        byte[] probe = new byte[InitialBufferSize];
        int code = read(probe, InitialBufferSize, out required);
        if (code == (int)SimResultCode.BufferTooSmall)
        {
            for (int attempt = 0; attempt < MaxAttempts; attempt++)
            {
                byte[] buffer = new byte[checked((int)required)];
                code = read(buffer, (nuint)buffer.Length, out nuint written);
                if (code == (int)SimResultCode.BufferTooSmall)
                {
                    required = written;
                    continue;
                }

                ThrowForResult(code, operation);
                return Decode(buffer, written);
            }

            throw new SimNativeException(
                SimResultCode.InternalError, $"{operation}失败：缓冲需求连续增长，核心输出异常。");
        }

        ThrowForResult(code, operation);
        // 首探即成功（内容未超 InitialBufferSize）：required 为实际写出长度
        // （不含 NUL），必须解析首探缓冲，不能丢弃真实内容。
        return Decode(probe, required);
    }

    /// <summary>把 wfs_sim_result 错误码映射为可操作的中文异常（桥内各调用复用）。</summary>
    /// <param name="rawCode">原始错误码。</param>
    /// <param name="operation">操作中文名。</param>
    internal static void ThrowForResult(int rawCode, string operation)
    {
        var code = (SimResultCode)rawCode;
        if (code == SimResultCode.Ok)
        {
            return;
        }

        string hint = code switch
        {
            SimResultCode.InvalidData => "（核心校验拒绝，最终以核心为准）",
            SimResultCode.IoError => "（请检查文件路径与磁盘状态）",
            _ => string.Empty,
        };
        throw new SimNativeException(code, $"{operation}失败：{SimNativeException.Describe(code)}{hint}。");
    }

    private static string Decode(byte[] buffer, nuint length)
    {
        // 契约：OK 时 out_len 不含 NUL；TrimEnd 只是脏缓冲防御，不改变正常文本。
        return Encoding.UTF8.GetString(buffer, 0, checked((int)length)).TrimEnd('\0');
    }
}
