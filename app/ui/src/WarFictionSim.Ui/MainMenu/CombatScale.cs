// 文件总览：主菜单 —— 作战规模枚举与解析（T039）。
//
// 规模是 v1 的两档选择（FR-001：连排级/营级，旅级后置）。场景文件用可选
// 扩展字段 scale 声明规模；缺失默认连排级（与现有教程/测试场景兼容，
// 也保持场景 Schema 的 additionalProperties 兼容性，不改动 data/）。

namespace WarFictionSim.Ui.MainMenu;

/// <summary>v1 可扮演的作战规模。</summary>
public enum CombatScale
{
    /// <summary>连排级（最小可指挥单位为班/单车）。</summary>
    Platoon,

    /// <summary>营级（最小可指挥单位为连排级单位）。</summary>
    Battalion,
}

/// <summary>作战规模的显示与解析辅助。</summary>
public static class CombatScaleText
{
    /// <summary>返回面向玩家的中文名称。</summary>
    /// <param name="scale">作战规模。</param>
    /// <returns>中文名称，如"连排级"。</returns>
    public static string ToDisplayName(this CombatScale scale) => scale switch
    {
        CombatScale.Platoon => "连排级",
        CombatScale.Battalion => "营级",
        _ => "未知规模",
    };

    /// <summary>按场景扩展字段解析规模；未知值返回 <see langword="false"/>。</summary>
    /// <param name="text">场景 JSON 的 scale 字段值（可为空）。</param>
    /// <param name="scale">解析结果；失败时为连排级默认。</param>
    /// <returns>解析成功（含空值默认）返回 <see langword="true"/>。</returns>
    public static bool TryParse(string? text, out CombatScale scale)
    {
        switch (text)
        {
            case null:
            case "":
            case "platoon":
                scale = CombatScale.Platoon;
                return true;
            case "battalion":
                scale = CombatScale.Battalion;
                return true;
            default:
                scale = CombatScale.Platoon;
                return false;
        }
    }
}
