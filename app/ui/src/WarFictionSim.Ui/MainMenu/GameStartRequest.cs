// 文件总览：主菜单 —— 战斗启动请求（T039）。
//
// 主菜单只负责"把玩家的选择解析为一次启动"，不创建句柄；应用壳消费本请求
// 完成工厂创建/读档。请求是只读 record，事件参数只承载纯数据（宪法第 14 条）。

namespace WarFictionSim.Ui.MainMenu;

/// <summary>一次新游戏/读档的完整启动参数。</summary>
/// <param name="Scenario">目标场景目录条目。</param>
/// <param name="Seed">随机种子（读档时为存档头种子）。</param>
/// <param name="Threads">并行度（≥1，只影响性能）。</param>
/// <param name="PlayerNodeId">玩家扮演的指挥节点 id。</param>
/// <param name="SavePath">读档时为存档路径；新游戏为 <see langword="null"/>。</param>
public sealed record GameStartRequest(
    ScenarioCatalogEntry Scenario,
    ulong Seed,
    int Threads,
    string PlayerNodeId,
    string? SavePath);

/// <summary>携带启动请求的事件参数。</summary>
/// <param name="Request">已校验通过的启动请求。</param>
public sealed class GameStartRequestedEventArgs(GameStartRequest Request) : EventArgs
{
    /// <summary>启动请求。</summary>
    public GameStartRequest Request { get; } = Request;
}
