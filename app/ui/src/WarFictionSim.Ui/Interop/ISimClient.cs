// 文件总览：UI↔核心互操作 —— 模拟客户端抽象接口（T043）。
//
// 为什么抽象成接口：ViewModel 只依赖这个最小表面，单元测试用假实现注入；
// 接口刻意窄化——快照与事件查询只读；模拟状态变更（命令注入/读档恢复）
// 统一经本接口，UI 不直接修改模拟状态（宪法第 14 条）。

namespace WarFictionSim.Ui.Interop;

/// <summary>表现层可用的模拟核心最小接口（句柄生命周期由实现管理）。</summary>
public interface ISimClient : IDisposable
{
    /// <summary>核心 ABI 版本（wfs_sim_version，创建句柄时读取并缓存）。</summary>
    string AbiVersion { get; }

    /// <summary>读取一份只读快照（解析为不可变 DTO，禁止写回模拟）。</summary>
    /// <returns>不可变快照。</returns>
    /// <exception cref="SimNativeException">核心返回非 OK 错误码。</exception>
    SimulationSnapshot GetSnapshot();

    /// <summary>按过滤器查询事件日志（wfs_sim_query_events，只读）。</summary>
    /// <param name="queryJson">可选 category/min_severity/text/limit 的查询 JSON。</param>
    /// <returns>核心输出的只读 JSON 文本：events/count/truncated。</returns>
    /// <exception cref="SimNativeException">查询参数非法或核心返回非 OK 错误码。</exception>
    string QueryEvents(string queryJson);

    /// <summary>读取 64 位十六进制状态哈希（小写，SHA-256）。</summary>
    /// <returns>状态哈希字符串。</returns>
    string GetStateHash();

    /// <summary>推进一个离散游戏 tick（确定性，不读现实时钟）。</summary>
    void Step();

    /// <summary>注入玩家命令——UI 修改模拟状态的唯一写路径，最终校验以核心为准。</summary>
    /// <param name="commandJson">符合 contracts/schemas/command.schema.json 的命令 JSON。</param>
    /// <exception cref="SimNativeException">核心拒绝命令（InvalidData 等）。</exception>
    void InjectCommand(string commandJson);

    /// <summary>把当前状态写入 WFS-SAVE 存档文件。</summary>
    /// <param name="path">目标存档路径。</param>
    void Save(string path);

    /// <summary>从 WFS-SAVE 存档恢复状态（失败不改写句柄）。</summary>
    /// <param name="path">存档路径。</param>
    void LoadSave(string path);
}
