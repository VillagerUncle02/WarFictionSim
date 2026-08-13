// 文件总览：UI↔核心互操作 —— 胜负判定状态 DTO（T043，只读消费）。
//
// 镜像 native/sim/src/outcome.cpp OutcomeState 的 to_json 输出（kind 为
// undecided/victory/defeat 字符串），供结算页与 HUD 只读展示。

namespace WarFictionSim.Ui.Interop;

/// <summary>胜负判定状态（decided 后冻结）。</summary>
public sealed record OutcomeState(
    bool Decided,
    string Kind,
    double CompletionRatio,
    string Reason,
    ulong DecidedTick);
