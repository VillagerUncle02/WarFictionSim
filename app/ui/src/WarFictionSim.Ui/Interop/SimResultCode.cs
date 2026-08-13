// 文件总览：UI↔核心互操作 —— C ABI 错误码镜像（T043）。
//
// 与 native/sim/include/wfs/sim/c_api.h 的 wfs_sim_result 枚举逐值对应；
// 表现层只翻译错误码，不重新定义语义，保证跨层报错可对照（宪法第 17 条）。

namespace WarFictionSim.Ui.Interop;

/// <summary>模拟核心 C ABI 返回码（与 wfs_sim_result 逐值一致）。</summary>
public enum SimResultCode
{
    /// <summary>成功。</summary>
    Ok = 0,

    /// <summary>参数非法（空句柄/空指针/线程数小于 1 等）。</summary>
    InvalidArgument = 1,

    /// <summary>文件读写失败。</summary>
    IoError = 2,

    /// <summary>数据非法（校验失败、损坏存档、非法命令）。</summary>
    InvalidData = 3,

    /// <summary>快照缓冲不足（out_len 返回所需字节数）。</summary>
    BufferTooSmall = 4,

    /// <summary>功能未实现。</summary>
    NotImplemented = 5,

    /// <summary>核心内部错误（异常被边界层捕获后映射）。</summary>
    InternalError = 6,
}
