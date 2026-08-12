// 文件总览：UI↔核心互操作 —— 快照 JSON 解析器（T043，只读消费）。
//
// 为什么用手写解析而非直接反序列化到可变对象：字段键必须与 native
// snapshot.cpp 严格一致，且解析失败要携带字段路径给出可定位错误
// （宪法第 17 条）；产物全部是 init-only/只读集合，从类型层面阻止
// 表现层写回模拟状态（宪法第 14 条）。两种入口（UTF-8 字节 / 字符串）
// 共用同一文档解析，保证语义一致。

using System.Text.Json;

namespace WarFictionSim.Ui.Interop;

/// <summary>把核心输出的只读快照 JSON 解析为不可变 DTO。</summary>
public static class SnapshotReader
{
    /// <summary>解析 UTF-8 字节快照（原生缓冲直通路径，零拷贝转文本）。</summary>
    /// <param name="utf8Json">wfs_sim_get_snapshot 输出的 JSON 字节（不含 NUL）。</param>
    /// <returns>不可变快照。</returns>
    /// <exception cref="SnapshotParseException">JSON 非法或字段缺失/类型不符。</exception>
    public static SimulationSnapshot Parse(byte[] utf8Json) => Parse(utf8Json.AsMemory());

    /// <summary>解析 UTF-8 字节快照。</summary>
    /// <param name="utf8Json">wfs_sim_get_snapshot 输出的 JSON 字节。</param>
    /// <returns>不可变快照。</returns>
    /// <exception cref="SnapshotParseException">JSON 非法或字段缺失/类型不符。</exception>
    public static SimulationSnapshot Parse(ReadOnlyMemory<byte> utf8Json)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(utf8Json);
            return ParseDocument(document.RootElement);
        }
        catch (SnapshotParseException)
        {
            throw;
        }
        catch (JsonException exception)
        {
            throw new SnapshotParseException($"快照不是合法 JSON：{exception.Message}", exception);
        }
    }

    /// <summary>解析 JSON 文本快照。</summary>
    /// <param name="json">快照 JSON 文本。</param>
    /// <returns>不可变快照。</returns>
    /// <exception cref="SnapshotParseException">JSON 非法或字段缺失/类型不符。</exception>
    public static SimulationSnapshot Parse(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        return Parse(System.Text.Encoding.UTF8.GetBytes(json));
    }

    private static SimulationSnapshot ParseDocument(JsonElement root)
    {
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new SnapshotParseException("快照根节点必须是 JSON 对象。");
        }

        var units = new List<UnitState>();
        foreach (JsonElement unitElement in RequireArray(root, "units", "$"))
        {
            units.Add(ParseUnit(unitElement, "$.units[]"));
        }

        var intel = new Dictionary<string, IntelRecordState>();
        foreach (JsonProperty recordProperty in RequireObject(root, "intel_records", "$").EnumerateObject())
        {
            intel.Add(recordProperty.Name, ParseIntelRecord(recordProperty.Value, $"$.intel_records.{recordProperty.Name}"));
        }

        var objectives = new List<ObjectiveState>();
        foreach (JsonElement objectiveElement in RequireArray(root, "objectives", "$"))
        {
            objectives.Add(ParseObjective(objectiveElement, "$.objectives[]"));
        }

        return new SimulationSnapshot
        {
            AbiVersion = RequireString(root, "abi_version", "$"),
            Tick = RequireUInt64(root, "tick", "$"),
            TotalUs = RequireUInt64(root, "total_us", "$"),
            Seed = RequireUInt64(root, "seed", "$"),
            Threads = RequireInt32(root, "threads", "$"),
            ScenarioId = RequireString(root, "scenario_id", "$"),
            ScenarioName = RequireString(root, "scenario_name", "$"),
            PlayerNodeId = RequireString(root, "player_node_id", "$"),
            PendingEvents = RequireUInt64(root, "pending_events", "$"),
            ProcessedEvents = RequireUInt64(root, "processed_events", "$"),
            EventLog = ParseEventLogSummary(RequireObject(root, "event_log", "$"), "$.event_log"),
            CommandChain = ParseCommandChainSummary(RequireObject(root, "command_chain", "$"), "$.command_chain"),
            Units = new System.Collections.ObjectModel.ReadOnlyCollection<UnitState>(units),
            IntelRecords = new System.Collections.ObjectModel.ReadOnlyDictionary<string, IntelRecordState>(intel),
            Objectives = new System.Collections.ObjectModel.ReadOnlyCollection<ObjectiveState>(objectives),
            Outcome = ParseOutcome(RequireObject(root, "outcome", "$"), "$.outcome"),
        };
    }

    private static UnitState ParseUnit(JsonElement element, string path)
    {
        var ammo = new Dictionary<string, ulong>();
        foreach (JsonProperty ammoProperty in RequireObject(element, "ammo", path).EnumerateObject())
        {
            ammo.Add(ammoProperty.Name, ammoProperty.Value.GetUInt64());
        }

        int soldierCount = RequireArray(element, "soldiers", path).Count;
        return new UnitState
        {
            Id = RequireString(element, "id", path),
            Type = RequireString(element, "type", path),
            NodeId = RequireString(element, "node_id", path),
            Side = RequireString(element, "side", path),
            X = RequireDouble(element, "x", path),
            Y = RequireDouble(element, "y", path),
            Formation = RequireString(element, "formation", path),
            Cover = RequireString(element, "cover", path),
            Suppression = RequireDouble(element, "suppression", path),
            LastKnownX = RequireDouble(element, "last_known_x", path),
            LastKnownY = RequireDouble(element, "last_known_y", path),
            LastKnownSuppression = RequireDouble(element, "last_known_suppression", path),
            LastKnownFormation = RequireString(element, "last_known_formation", path),
            HasLastKnown = RequireBoolean(element, "has_last_known", path),
            LastKnownDestroyed = RequireBoolean(element, "last_known_destroyed", path),
            OutOfContact = RequireBoolean(element, "out_of_contact", path),
            ContactTicksRemaining = RequireUInt64(element, "contact_ticks_remaining", path),
            Destroyed = RequireBoolean(element, "destroyed", path),
            IsVehicle = RequireBoolean(element, "is_vehicle", path),
            Amphibious = RequireBoolean(element, "amphibious", path),
            VehicleDamage = RequireDouble(element, "vehicle_damage", path),
            VehicleHp = RequireDouble(element, "vehicle_hp", path),
            SoldierCount = soldierCount,
            CrewCount = RequireInt32(element, "crew_count", path),
            Ammo = new System.Collections.ObjectModel.ReadOnlyDictionary<string, ulong>(ammo),
            MissionActive = RequireBoolean(element, "mission_active", path),
            MissionCommandId = RequireString(element, "mission_command_id", path),
            MissionType = RequireString(element, "mission_type", path),
            MissionPriority = RequireInt64(element, "mission_priority", path),
            MissionDeadlineTicks = RequireUInt64(element, "mission_deadline_ticks", path),
            MissionCondition = RequireString(element, "mission_condition", path),
            MissionLoops = RequireBoolean(element, "mission_loops", path),
            AmmoPolicy = RequireString(element, "ammo_policy", path),
            AmmoOverride = RequireString(element, "ammo_override", path),
            FailureAction = RequireString(element, "failure_action", path),
            FailureTarget = RequireString(element, "failure_target", path),
            Moving = RequireBoolean(element, "moving", path),
            TargetX = RequireDouble(element, "target_x", path),
            TargetY = RequireDouble(element, "target_y", path),
            Stuck = RequireBoolean(element, "stuck", path),
        };
    }

    private static IntelRecordState ParseIntelRecord(JsonElement element, string path)
    {
        JsonElement sourceElement = RequireObject(element, "source", path);
        var source = new IntelSourceState(
            RequireString(sourceElement, "kind", path + ".source"),
            RequireString(sourceElement, "unit_id", path + ".source"),
            RequireString(sourceElement, "node_id", path + ".source"),
            RequireUInt64(sourceElement, "reported_tick", path + ".source"));

        return new IntelRecordState(
            RequireString(element, "observer_node_id", path),
            RequireString(element, "target_unit_id", path),
            ParseTier(RequireString(element, "tier", path), path),
            RequireUInt64(element, "last_seen_tick", path),
            RequireUInt64(element, "memory_until_tick", path),
            RequireUInt64(element, "source_expires_tick", path),
            source,
            RequireDouble(element, "last_known_x", path),
            RequireDouble(element, "last_known_y", path),
            RequireDouble(element, "last_motion_dx", path),
            RequireDouble(element, "last_motion_dy", path),
            TryGetOptionalUInt64(element, "observed_count", path),
            TryGetOptionalString(element, "type_name", path),
            TryGetOptionalString(element, "composition", path));
    }

    private static ObjectiveState ParseObjective(JsonElement element, string path) =>
        new(
            RequireString(element, "id", path),
            RequireString(element, "kind", path),
            RequireString(element, "target_ref", path),
            RequireUInt64(element, "duration_ticks", path),
            RequireUInt64(element, "hold_ticks", path),
            RequireBoolean(element, "completed", path));

    private static OutcomeState ParseOutcome(JsonElement element, string path) =>
        new(
            RequireBoolean(element, "decided", path),
            RequireString(element, "kind", path),
            RequireDouble(element, "completion_ratio", path),
            RequireString(element, "reason", path),
            RequireUInt64(element, "decided_tick", path));

    private static EventLogSummaryState ParseEventLogSummary(JsonElement element, string path) =>
        new(
            RequireUInt64(element, "size", path),
            RequireUInt64(element, "capacity", path),
            RequireUInt64(element, "critical_count", path));

    private static CommandChainSummaryState ParseCommandChainSummary(JsonElement element, string path) =>
        new(RequireUInt64(element, "commands", path));

    private static IntelTier ParseTier(string value, string path) => value switch
    {
        "none" => IntelTier.None,
        "T1" => IntelTier.T1,
        "T2" => IntelTier.T2,
        "T3" => IntelTier.T3,
        _ => throw new SnapshotParseException($"快照解析失败（{path}.tier）：未知识别档位 '{value}'。"),
    };

    private static JsonElement RequireObject(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Object)
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少对象字段。");
        }

        return value;
    }

    private static List<JsonElement> RequireArray(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Array)
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少数组字段。");
        }

        var items = new List<JsonElement>();
        foreach (JsonElement item in value.EnumerateArray())
        {
            items.Add(item);
        }

        return items;
    }

    private static string RequireString(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String)
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少字符串字段。");
        }

        return value.GetString() ?? throw new SnapshotParseException($"快照解析失败（{path}.{name}）：字符串为空。");
    }

    private static ulong RequireUInt64(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number ||
            !value.TryGetUInt64(out ulong result))
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少非负整数字段。");
        }

        return result;
    }

    private static long RequireInt64(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number ||
            !value.TryGetInt64(out long result))
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少整数字段。");
        }

        return result;
    }

    private static int RequireInt32(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number ||
            !value.TryGetInt32(out int result))
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少整数字段。");
        }

        return result;
    }

    private static double RequireDouble(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number ||
            !value.TryGetDouble(out double result))
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少数值字段。");
        }

        return result;
    }

    private static bool RequireBoolean(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) ||
            (value.ValueKind != JsonValueKind.True && value.ValueKind != JsonValueKind.False))
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：缺少布尔字段。");
        }

        return value.GetBoolean();
    }

    private static ulong? TryGetOptionalUInt64(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value))
        {
            return null;
        }

        if (value.ValueKind != JsonValueKind.Number || !value.TryGetUInt64(out ulong result))
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：字段类型必须为非负整数。");
        }

        return result;
    }

    private static string? TryGetOptionalString(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value))
        {
            return null;
        }

        if (value.ValueKind != JsonValueKind.String)
        {
            throw new SnapshotParseException($"快照解析失败（{path}.{name}）：字段类型必须为字符串。");
        }

        return value.GetString() ?? throw new SnapshotParseException($"快照解析失败（{path}.{name}）：字符串为空。");
    }
}

/// <summary>快照 JSON 解析失败（携带字段路径，可定位）。</summary>
public sealed class SnapshotParseException : Exception
{
    /// <summary>初始化异常。</summary>
    /// <param name="message">含字段路径的中文说明。</param>
    public SnapshotParseException(string message)
        : base(message)
    {
    }

    /// <summary>初始化异常并保留内部异常链。</summary>
    /// <param name="message">含字段路径的中文说明。</param>
    /// <param name="innerException">底层 JSON 异常。</param>
    public SnapshotParseException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
