// 文件总览：命令面板 —— 命令 JSON 构造（T041）。
//
// 输出必须与 contracts/schemas/command.schema.json 完全一致，键序固定
// （schema_version → type → target → completion → intent → behavior →
// priority → deadline），便于黄金字符串测试与核心确定性校验。

using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>把命令草稿序列化为核心可注入的命令 JSON。</summary>
public static class CommandJsonBuilder
{
    /// <summary>序列化草稿（调用前应已通过三级校验，类型/条件必填）。</summary>
    /// <param name="draft">命令草稿。</param>
    /// <returns>符合 command.schema.json 的命令 JSON 文本。</returns>
    /// <exception cref="ArgumentException">类型或完成条件缺失。</exception>
    public static string Build(CommandDraft draft)
    {
        ArgumentNullException.ThrowIfNull(draft);
        if (string.IsNullOrWhiteSpace(draft.Type) || string.IsNullOrWhiteSpace(draft.Condition))
        {
            throw new ArgumentException("命令类型与完成条件缺失，不能序列化（应先经三级校验）。");
        }

        using var stream = new MemoryStream();
        // 中文按 UTF-8 原样输出（不转义为 \uXXXX）：与核心 nlohmann::json dump
        // 的行为一致，便于玩家与 AI 阅读命令正文，也便于黄金字符串比对。
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions
        {
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        }))
        {
            writer.WriteStartObject();
            writer.WriteNumber("schema_version", 1);
            writer.WriteString("type", draft.Type);
            WriteTarget(writer, draft);
            WriteCompletion(writer, draft);
            writer.WriteString("intent", draft.Intent ?? string.Empty);
            WriteBehavior(writer, draft);
            writer.WriteNumber("priority", draft.Priority);
            writer.WriteStartObject("deadline");
            writer.WriteNumber("game_time", draft.DeadlineTick);
            writer.WriteEndObject();
            writer.WriteEndObject();
        }

        return Encoding.UTF8.GetString(stream.ToArray());
    }

    private static void WriteTarget(Utf8JsonWriter writer, CommandDraft draft)
    {
        writer.WriteStartObject("target");
        if (draft.ExecutorIds.Count == 1)
        {
            writer.WriteString("kind", "unit");
            writer.WriteString("ref", draft.ExecutorIds[0]);
        }
        else
        {
            writer.WriteString("kind", "units");
            writer.WriteStartArray("refs");
            foreach (string executorId in draft.ExecutorIds)
            {
                writer.WriteStringValue(executorId);
            }

            writer.WriteEndArray();
        }

        writer.WriteEndObject();
    }

    private static void WriteCompletion(Utf8JsonWriter writer, CommandDraft draft)
    {
        writer.WriteStartObject("completion");
        writer.WriteString("condition", draft.Condition);
        writer.WriteStartObject("params");
        switch (draft.Condition)
        {
            case "secure_zone":
            case "drive_out":
            case "clear":
                writer.WriteString("zone", draft.ZoneId ?? string.Empty);
                if (draft.DurationTicks is > 0)
                {
                    writer.WriteNumber("duration_ticks", draft.DurationTicks.Value);
                }

                break;
            case "destroy_unit":
                writer.WriteString("target_unit", draft.TargetUnitId ?? string.Empty);
                break;
            case "hold":
                writer.WriteNumber("duration_ticks", draft.DurationTicks ?? 0);
                break;
            case "reach_point":
                WritePoint(writer, "point", draft.PointX ?? 0, draft.PointY ?? 0);
                break;
            case "patrol":
                writer.WriteNumber("cycle_ticks", draft.CycleTicks ?? 0);
                break;
            case "fortify":
                writer.WriteNumber("construction_ticks", draft.ConstructionTicks ?? 0);
                break;
            case "recon":
                WritePoint(writer, "point", draft.PointX ?? 0, draft.PointY ?? 0);
                if (draft.ExitPointX.HasValue && draft.ExitPointY.HasValue)
                {
                    WritePoint(writer, "exit_point", draft.ExitPointX.Value, draft.ExitPointY.Value);
                }

                break;
        }

        writer.WriteEndObject();
        writer.WriteEndObject();
    }

    private static void WritePoint(Utf8JsonWriter writer, string name, double x, double y)
    {
        writer.WriteStartObject(name);
        writer.WriteNumber("x", x);
        writer.WriteNumber("y", y);
        writer.WriteEndObject();
    }

    private static void WriteBehavior(Utf8JsonWriter writer, CommandDraft draft)
    {
        writer.WriteStartObject("behavior");
        writer.WriteString("engagement", draft.Engagement);
        if (!string.IsNullOrWhiteSpace(draft.Formation))
        {
            writer.WriteString("formation", draft.Formation);
        }

        if (!string.IsNullOrWhiteSpace(draft.AmmoOverride))
        {
            writer.WriteString("ammo_override", draft.AmmoOverride);
        }

        writer.WriteString("failure_action", draft.FailureAction);
        if (!string.IsNullOrWhiteSpace(draft.FailureTarget))
        {
            writer.WriteString("failure_target", draft.FailureTarget);
        }

        writer.WriteEndObject();
    }
}
