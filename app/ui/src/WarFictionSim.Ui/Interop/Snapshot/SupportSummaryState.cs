// 文件总览：UI↔核心互操作 —— 支援/配属摘要 DTO（T053，只读消费）。
//
// 字段镜像 native/sim/src/snapshot.cpp build_snapshot_json 的 support 摘要
// 键（configured/scale/faction_id/pool_echelon/pending_requests/attaches/
// score_remaining）；可用支援种类清单不在快照内（资源池是派系模板静态
// 数据），由场景目录另行投影（ScenarioCatalogEntry.SupportKinds）。

namespace WarFictionSim.Ui.Interop;

/// <summary>支援/配属快照摘要（只读）。</summary>
/// <param name="Configured">场景是否配置支援管线。</param>
/// <param name="Scale">作战规模（platoon/battalion）。</param>
/// <param name="FactionId">支援派系模板 id。</param>
/// <param name="PoolEchelon">生效资源池编制层级（battalion）。</param>
/// <param name="PendingRequests">评估中请求数。</param>
/// <param name="Attaches">在编配属记录数。</param>
/// <param name="ScoreRemaining">连排级剩余支援分数（扣减用）；营级快照仍
/// 携带池额度（support_score），但按配属链裁决不参与扣减（复审 R1-2）。</param>
public sealed record SupportSummaryState(
    bool Configured,
    string Scale,
    string FactionId,
    string PoolEchelon,
    ulong PendingRequests,
    ulong Attaches,
    ulong ScoreRemaining);
