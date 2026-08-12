// 文件总览：UI↔核心互操作 —— 存档头 DTO（T043/T039 读档校验）。
//
// 镜像 native/sim/src/save.cpp BuildHeader 的 JSON 字段；主菜单读档前先
// 解析本头做"格式版本 + ABI 版本"前置校验，未通过绝不创建句柄（宪法 13/17）。

namespace WarFictionSim.Ui.Interop;

/// <summary>WFS-SAVE 存档头元数据（只读）。</summary>
public sealed class SaveHeader
{
    /// <summary>存档格式版本（u32 LE）。</summary>
    public required uint FormatVersion { get; init; }

    /// <summary>写入存档时的核心 ABI 版本。</summary>
    public required string AbiVersion { get; init; }

    /// <summary>存档所属场景 id。</summary>
    public required string ScenarioId { get; init; }

    /// <summary>存档所属场景显示名。</summary>
    public required string ScenarioName { get; init; }

    /// <summary>存档时刻的游戏 tick。</summary>
    public required ulong Tick { get; init; }

    /// <summary>该局随机种子（加载时必须与句柄种子一致）。</summary>
    public required ulong Seed { get; init; }

    /// <summary>写入时的并行度配置（仅元数据，不参与哈希）。</summary>
    public required int Threads { get; init; }

    /// <summary>场景 schema_version。</summary>
    public required long SchemaVersion { get; init; }

    /// <summary>状态哈希算法名（当前恒为 SHA-256）。</summary>
    public required string StateHashAlgorithm { get; init; }

    /// <summary>state_blob 字节数。</summary>
    public required ulong StateSizeBytes { get; init; }
}
