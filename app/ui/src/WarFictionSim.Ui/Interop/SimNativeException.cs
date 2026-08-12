// 文件总览：UI↔核心互操作 —— 原生调用异常类型（T043）。
//
// 为什么需要专用异常：P/Invoke 边界失败必须携带错误码与可操作的上下文，
// 让 UI 能把"为什么失败、怎么修复"直接呈现给玩家（宪法第 17 条不静默吞错），
// 而不是把裸 DllNotFoundException/错误码往上抛。

namespace WarFictionSim.Ui.Interop;

/// <summary>模拟核心原生调用失败（携带 C ABI 错误码与中文说明）。</summary>
public sealed class SimNativeException : Exception
{
    /// <summary>初始化异常。</summary>
    /// <param name="resultCode">核心返回的错误码。</param>
    /// <param name="message">面向玩家的中文说明。</param>
    public SimNativeException(SimResultCode resultCode, string message)
        : base(message)
    {
        ResultCode = resultCode;
    }

    /// <summary>初始化异常并保留内部异常链。</summary>
    /// <param name="resultCode">核心返回的错误码。</param>
    /// <param name="message">面向玩家的中文说明。</param>
    /// <param name="innerException">底层异常（如 DllNotFoundException）。</param>
    public SimNativeException(SimResultCode resultCode, string message, Exception innerException)
        : base(message, innerException)
    {
        ResultCode = resultCode;
    }

    /// <summary>核心返回的错误码。</summary>
    public SimResultCode ResultCode { get; }

    /// <summary>把错误码翻译为稳定的中文短语（用于错误提示拼接）。</summary>
    /// <param name="code">待翻译的错误码。</param>
    /// <returns>中文短语，如"参数非法"。</returns>
    public static string Describe(SimResultCode code) => code switch
    {
        SimResultCode.Ok => "成功",
        SimResultCode.InvalidArgument => "参数非法",
        SimResultCode.IoError => "文件读写失败",
        SimResultCode.InvalidData => "数据校验失败",
        SimResultCode.BufferTooSmall => "缓冲区不足",
        SimResultCode.NotImplemented => "功能未实现",
        SimResultCode.InternalError => "模拟核心内部错误",
        _ => "未知错误",
    };
}
