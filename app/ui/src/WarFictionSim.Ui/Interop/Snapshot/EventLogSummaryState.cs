// 文件总览：UI↔核心互操作 —— 事件日志摘要 DTO（T043，只读消费）。
//
// 快照只携带日志计数摘要（size/capacity/critical_count），不含条目正文
// （native snapshot.cpp 的取舍，保证快照体积有界）；事件正文经独立通道
// 供给 EventLogPanel（T042），面板状态栏可用本摘要显示"已记录/关键"数。

namespace WarFictionSim.Ui.Interop;

/// <summary>事件日志保留摘要。</summary>
public sealed record EventLogSummaryState(
    ulong Size,
    ulong Capacity,
    ulong CriticalCount);
