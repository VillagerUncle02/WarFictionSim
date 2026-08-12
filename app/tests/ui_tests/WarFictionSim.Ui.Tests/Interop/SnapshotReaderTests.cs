// 测试：UI↔核心互操作 —— 快照只读解析与只读边界（T043）。
//
// 快照是表现层唯一的数据入口（contracts/sim-c-api.md：只读 JSON、调用方负责
// 缓冲生命周期）。这里验证三件事：JSON 文本/UTF-8 字节两种解析入口等价、
// 字段与 native snapshot.cpp 的键严格一致、解析产物不可写回（宪法第 14 条）。

using System.Reflection;
using System.Text;
using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.Interop;

public class SnapshotReaderTests
{
    // 与 native/sim/src/snapshot.cpp build_snapshot_json + sim_state.h to_json 对齐的样例。
    private const string SnapshotJson = """
        {
          "abi_version": "0.1.0",
          "tick": 120,
          "total_us": 6000000,
          "seed": 42,
          "threads": 4,
          "scenario_id": "scn-tutorial-platoon",
          "scenario_name": "Tutorial: Platoon Attack",
          "player_node_id": "node-tutorial-platoon",
          "pending_events": 3,
          "processed_events": 99,
          "event_log": { "size": 7, "capacity": 5000, "critical_count": 2 },
          "command_chain": { "commands": 4 },
          "units": [
            {
              "id": "tutorial-squad-1",
              "type": "squad-rifle-us",
              "node_id": "node-tutorial-platoon",
              "side": "node-tutorial-platoon",
              "x": 0.35, "y": 0.3,
              "formation": "march", "cover": "none", "suppression": 0.1,
              "last_known_x": 0.35, "last_known_y": 0.3,
              "last_known_suppression": 0.0,
              "last_known_formation": "march",
              "has_last_known": false, "last_known_destroyed": false,
              "out_of_contact": false, "contact_ticks_remaining": 0,
              "destroyed": false, "is_vehicle": false, "amphibious": true,
              "vehicle_damage": 0.0, "vehicle_hp": 100.0,
              "weapons": [], "ammo": { "ammo-556": 240, "ammo-frag-grenade": 8 },
              "soldiers": [ { "id": "soldier-1" }, { "id": "soldier-2" } ],
              "crew_count": 2,
              "mission_active": true, "mission_command_id": "cmd-7",
              "mission_type": "SECURE_ZONE", "mission_priority": 1,
              "mission_deadline_ticks": 3600, "mission_condition": "secure_zone",
              "mission_params": { "zone": "zone-objective-hill", "duration_ticks": 2400 },
              "ammo_policy": "auto", "ammo_override": "", "failure_action": "report",
              "failure_target": "", "mission_loops": true,
              "moving": true, "target_x": 1.1, "target_y": 1.1, "stuck": false
            },
            {
              "id": "tutorial-enemy-squad-1",
              "type": "squad-rifle-opposition",
              "node_id": "node-tutorial-enemy",
              "side": "node-tutorial-enemy",
              "x": 1.15, "y": 1.2,
              "formation": "combat", "cover": "forest", "suppression": 0.0,
              "last_known_x": 1.15, "last_known_y": 1.2,
              "last_known_suppression": 0.0,
              "last_known_formation": "combat",
              "has_last_known": true, "last_known_destroyed": false,
              "out_of_contact": false, "contact_ticks_remaining": 0,
              "destroyed": false, "is_vehicle": false, "amphibious": true,
              "vehicle_damage": 0.0, "vehicle_hp": 100.0,
              "weapons": [], "ammo": { "ammo-762": 200 },
              "soldiers": [ { "id": "soldier-3" } ],
              "crew_count": 1,
              "mission_active": false, "mission_command_id": "",
              "mission_type": "", "mission_priority": 0,
              "mission_deadline_ticks": 0, "mission_condition": "",
              "mission_params": {},
              "ammo_policy": "auto", "ammo_override": "", "failure_action": "report",
              "failure_target": "", "mission_loops": true,
              "moving": false, "target_x": 0.0, "target_y": 0.0, "stuck": false
            }
          ],
          "intel_records": {
            "node-tutorial-platoon:tutorial-enemy-squad-1": {
              "observer_node_id": "node-tutorial-platoon",
              "target_unit_id": "tutorial-enemy-squad-1",
              "tier": "T2",
              "last_seen_tick": 100,
              "memory_until_tick": 500,
              "source_expires_tick": 400,
              "source": { "kind": "direct", "unit_id": "tutorial-squad-1", "node_id": "node-tutorial-platoon", "reported_tick": 100 },
              "last_known_x": 1.15, "last_known_y": 1.2,
              "last_motion_dx": 0.6, "last_motion_dy": 0.8
            }
          },
          "objectives": [
            { "id": "obj-secure-hill", "kind": "zone", "target_ref": "zone-objective-hill",
              "duration_ticks": 2400, "hold_ticks": 0, "completed": false }
          ],
          "outcome": { "decided": false, "kind": "undecided", "completion_ratio": 0.0,
                       "reason": "", "decided_tick": 0 }
        }
        """;

    [Fact]
    public void Parse_FromString_ReadsAllContractFields()
    {
        SimulationSnapshot snapshot = SnapshotReader.Parse(SnapshotJson);

        Assert.Equal("0.1.0", snapshot.AbiVersion);
        Assert.Equal((ulong)120, snapshot.Tick);
        Assert.Equal((ulong)42, snapshot.Seed);
        Assert.Equal(4, snapshot.Threads);
        Assert.Equal("scn-tutorial-platoon", snapshot.ScenarioId);
        Assert.Equal("node-tutorial-platoon", snapshot.PlayerNodeId);
        Assert.Equal((ulong)3, snapshot.PendingEvents);
        Assert.Equal((ulong)99, snapshot.ProcessedEvents);
        Assert.Equal(new EventLogSummaryState(7, 5000, 2), snapshot.EventLog);
        Assert.Equal((ulong)4, snapshot.CommandChain.CommandCount);
        Assert.Equal(2, snapshot.Units.Count);
        Assert.Single(snapshot.Objectives);
        Assert.False(snapshot.Outcome.Decided);
    }

    [Fact]
    public void Parse_FromString_ReadsUnitFields()
    {
        SimulationSnapshot snapshot = SnapshotReader.Parse(SnapshotJson);
        UnitState unit = Assert.Single(snapshot.Units, u => u.Id == "tutorial-squad-1");

        Assert.Equal("squad-rifle-us", unit.Type);
        Assert.Equal("node-tutorial-platoon", unit.NodeId);
        Assert.Equal("node-tutorial-platoon", unit.Side);
        Assert.Equal(0.35, unit.X);
        Assert.Equal(0.3, unit.Y);
        Assert.Equal("march", unit.Formation);
        Assert.True(unit.Amphibious);
        Assert.True(unit.MissionActive);
        Assert.Equal("SECURE_ZONE", unit.MissionType);
        Assert.Equal((ulong)3600, unit.MissionDeadlineTicks);
        Assert.Equal((ulong)240, unit.Ammo["ammo-556"]);
        Assert.Equal(2, unit.SoldierCount);
        Assert.Equal(2, unit.CrewCount);
        Assert.True(unit.Moving);
    }

    [Fact]
    public void Parse_FromString_ReadsIntelRecord()
    {
        SimulationSnapshot snapshot = SnapshotReader.Parse(SnapshotJson);

        IntelRecordState record =
            Assert.Single(snapshot.IntelRecords.Values, r => r.TargetUnitId == "tutorial-enemy-squad-1");
        Assert.Equal(IntelTier.T2, record.Tier);
        Assert.Equal("direct", record.Source.Kind);
        Assert.Equal("tutorial-squad-1", record.Source.UnitId);
        Assert.Equal(0.6, record.LastMotionDx);
        Assert.Equal(0.8, record.LastMotionDy);
    }

    [Fact]
    public void Parse_IntelRecord_ReadsOptionalObservationFields()
    {
        const string fields =
            """
            "observed_count": 9, "type_name": "BRDM-2", "composition": "车组2+载员7",
            """;
        string json = SnapshotJson.Replace(
            "\"last_seen_tick\": 100,",
            fields + "\"last_seen_tick\": 100,",
            StringComparison.Ordinal);

        SimulationSnapshot snapshot = SnapshotReader.Parse(json);
        IntelRecordState record =
            Assert.Single(snapshot.IntelRecords.Values, r => r.TargetUnitId == "tutorial-enemy-squad-1");

        Assert.Equal((ulong)9, record.ObservedCount);
        Assert.Equal("BRDM-2", record.TypeName);
        Assert.Equal("车组2+载员7", record.Composition);
    }

    [Fact]
    public void Parse_IntelRecord_WithoutObservationFields_AreNull()
    {
        SimulationSnapshot snapshot = SnapshotReader.Parse(SnapshotJson);
        IntelRecordState record =
            Assert.Single(snapshot.IntelRecords.Values, r => r.TargetUnitId == "tutorial-enemy-squad-1");

        Assert.Null(record.ObservedCount);
        Assert.Null(record.TypeName);
        Assert.Null(record.Composition);
    }

    [Fact]
    public void Parse_FromUtf8Bytes_MatchesStringParse()
    {
        byte[] bytes = Encoding.UTF8.GetBytes(SnapshotJson);

        SimulationSnapshot fromBytes = SnapshotReader.Parse(bytes);
        SimulationSnapshot fromString = SnapshotReader.Parse(SnapshotJson);

        Assert.Equal(fromString.Tick, fromBytes.Tick);
        Assert.Equal(fromString.Units.Count, fromBytes.Units.Count);
        Assert.Equal(fromString.IntelRecords.Count, fromBytes.IntelRecords.Count);
        Assert.Equal(fromString.EventLog, fromBytes.EventLog);
    }

    [Fact]
    public void Parse_WithInvalidJson_ThrowsParseException()
    {
        SnapshotParseException exception =
            Assert.Throws<SnapshotParseException>(() => SnapshotReader.Parse("{ 不是 JSON"));

        Assert.Contains("快照", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Parse_WithMissingRequiredField_ThrowsParseException()
    {
        const string json = """{ "abi_version": "0.1.0", "tick": 1 }""";

        SnapshotParseException exception =
            Assert.Throws<SnapshotParseException>(() => SnapshotReader.Parse(json));

        Assert.Contains("快照解析失败", exception.Message, StringComparison.Ordinal);
        Assert.Contains("缺少", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Parse_WithUnknownTier_ThrowsParseException()
    {
        string json = SnapshotJson.Replace("\"tier\": \"T2\"", "\"tier\": \"T9\"", StringComparison.Ordinal);

        Assert.Throws<SnapshotParseException>(() => SnapshotReader.Parse(json));
    }

    [Fact]
    public void SnapshotDto_IsReadOnly()
    {
        // 只读边界：快照 DTO 不能有任何公开 setter，集合必须是只读接口。
        AssertReadOnly(typeof(SimulationSnapshot));
        AssertReadOnly(typeof(UnitState));
        AssertReadOnly(typeof(IntelRecordState));
        AssertReadOnly(typeof(IntelSourceState));
        AssertReadOnly(typeof(ObjectiveState));
        AssertReadOnly(typeof(OutcomeState));
        AssertReadOnly(typeof(EventLogSummaryState));
        AssertReadOnly(typeof(CommandChainSummaryState));
    }

    private static void AssertReadOnly(Type type)
    {
        foreach (PropertyInfo property in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            // 只读边界定义：属性要么没有 setter，要么只有 init-only setter
            // （构造期一次性赋值）；集合属性还必须是只读接口类型。
            if (property.SetMethod is not null)
            {
                bool initOnly = property.SetMethod.ReturnParameter
                    .GetRequiredCustomModifiers()
                    .Any(modifier => modifier == typeof(System.Runtime.CompilerServices.IsExternalInit));
                Assert.True(initOnly, $"{type.Name}.{property.Name} 必须是 init-only，禁止公开 setter");
            }

            if (property.PropertyType.IsAssignableTo(typeof(System.Collections.IEnumerable)) &&
                property.PropertyType != typeof(string))
            {
                Assert.True(
                    property.PropertyType.IsInterface && property.PropertyType.Name.StartsWith("IReadOnly", StringComparison.Ordinal),
                    $"{type.Name}.{property.Name} 集合属性必须声明为 IReadOnly* 接口");
            }
        }
    }
}
