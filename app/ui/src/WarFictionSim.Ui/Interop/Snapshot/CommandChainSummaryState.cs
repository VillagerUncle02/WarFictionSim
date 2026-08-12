// 文件总览：UI↔核心互操作 —— 命令链路摘要 DTO（T043，只读消费）。
//
// 镜像 snapshot.cpp 的 command_chain.commands 计数，供 HUD 显示在途命令数；
// 只读、不暴露队列内部结构。

namespace WarFictionSim.Ui.Interop;

/// <summary>命令链路摘要。</summary>
public sealed record CommandChainSummaryState(ulong CommandCount);
