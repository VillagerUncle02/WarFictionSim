// 文件总览：命令面板 —— 13 种任务类型目录（T041）。
//
// 类型清单镜像 native mission_registry 的 13 种任务（FR-042，SC-010 可扩展），
// 每类带中文显示名与默认完成条件；类型与条件在核心是独立注册表，这里只给
// 面板合理的默认值，最终校验以核心为准。

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>命令面板可选的任务类型。</summary>
/// <param name="Key">命令 JSON 的 type 值。</param>
/// <param name="DisplayName">中文显示名。</param>
/// <param name="DefaultCondition">默认完成条件。</param>
public sealed record CommandTypeOption(string Key, string DisplayName, string DefaultCondition);

/// <summary>v1 十三种任务类型与默认完成条件。</summary>
public static class CommandTypeCatalog
{
    /// <summary>全部类型（固定顺序，供下拉与测试确定性引用）。</summary>
    public static readonly IReadOnlyList<CommandTypeOption> All =
    [
        new("MOVE", "移动", "reach_point"),
        new("PATROL", "巡逻", "patrol"),
        new("ATTACK", "攻击", "destroy_unit"),
        new("DEFEND", "防守", "hold"),
        new("SECURE_ZONE", "控制区域", "secure_zone"),
        new("CLEAR", "清剿", "clear"),
        new("DRIVE_OUT", "驱逐", "drive_out"),
        new("FORTIFY", "构筑工事", "fortify"),
        new("HIDDEN_RECON", "隐蔽侦察", "recon"),
        new("INFILTRATE_RECON", "渗透侦察", "recon"),
        new("OBSERVATION_POST", "观察哨", "hold"),
        new("FIRE_RECON", "火力侦察", "recon"),
        new("SUPPORT_REQUEST", "支援请求", "support"),
    ];

    /// <summary>返回类型的默认完成条件（未注册类型返回 null）。</summary>
    /// <param name="typeKey">命令类型键。</param>
    /// <returns>默认完成条件；未知类型为 <see langword="null"/>。</returns>
    public static string? DefaultConditionFor(string? typeKey) =>
        All.FirstOrDefault(option => option.Key == typeKey)?.DefaultCondition;
}
