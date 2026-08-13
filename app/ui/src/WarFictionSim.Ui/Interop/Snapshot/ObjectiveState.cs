// 文件总览：UI↔核心互操作 —— 关键目标进度 DTO（T043，只读消费）。
//
// 镜像 native/sim/src/outcome.cpp ObjectiveRuntimeState 的 to_json 输出，
// 供结算/目标进度展示（T040 之后的表现层面板）只读使用。

namespace WarFictionSim.Ui.Interop;

/// <summary>单个关键目标的运行期进度。</summary>
public sealed record ObjectiveState(
    string Id,
    string Kind,
    string TargetRef,
    ulong DurationTicks,
    ulong HoldTicks,
    bool Completed);
