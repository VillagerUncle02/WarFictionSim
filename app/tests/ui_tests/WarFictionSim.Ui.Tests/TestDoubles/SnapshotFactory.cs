// 测试替身：直接构造快照 DTO 的辅助工厂（兵牌/命令面板测试用）。
//
// DTO 是 required + init-only 的纯数据对象，测试无需走 JSON 字符串即可
// 组装任意迷雾/情报场景，让断言聚焦表现层规则本身。

using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.Tests.TestDoubles;

public static class SnapshotFactory
{
    public static SimulationSnapshot Create(
        ulong tick,
        IReadOnlyList<UnitState> units,
        IReadOnlyDictionary<string, IntelRecordState>? intel = null,
        string playerNodeId = "node-player",
        SupportSummaryState? support = null)
    {
        return new SimulationSnapshot
        {
            AbiVersion = SimAbiVersion.Expected,
            Tick = tick,
            TotalUs = tick * 50_000,
            Seed = 42,
            Threads = 4,
            ScenarioId = "scn-test",
            ScenarioName = "测试场景",
            PlayerNodeId = playerNodeId,
            PendingEvents = 0,
            ProcessedEvents = 0,
            EventLog = new EventLogSummaryState(0, 5000, 0),
            CommandChain = new CommandChainSummaryState(0),
            Units = units,
            IntelRecords = intel ?? new Dictionary<string, IntelRecordState>(),
            Objectives = [],
            Outcome = new OutcomeState(false, "undecided", 0, string.Empty, 0),
            Support = support ?? new SupportSummaryState(false, "platoon", string.Empty, "battalion", 0, 0, 0),
        };
    }

    public static UnitState Unit(
        string id,
        string nodeId,
        string side,
        double x,
        double y,
        bool outOfContact = false,
        bool hasLastKnown = false,
        double lastKnownX = 0,
        double lastKnownY = 0,
        bool destroyed = false,
        bool isVehicle = false,
        IReadOnlyDictionary<string, ulong>? ammo = null) =>
        new()
        {
            Id = id,
            Type = isVehicle ? "vehicle-test" : "squad-test",
            NodeId = nodeId,
            Side = side,
            X = x,
            Y = y,
            Formation = "march",
            Cover = "none",
            Suppression = 0,
            LastKnownX = lastKnownX,
            LastKnownY = lastKnownY,
            LastKnownSuppression = 0,
            LastKnownFormation = "march",
            HasLastKnown = hasLastKnown,
            LastKnownDestroyed = false,
            OutOfContact = outOfContact,
            ContactTicksRemaining = 0,
            Destroyed = destroyed,
            IsVehicle = isVehicle,
            Amphibious = false,
            VehicleDamage = 0,
            VehicleHp = 100,
            SoldierCount = 9,
            CrewCount = 9,
            Ammo = ammo ?? new Dictionary<string, ulong>(),
            MissionActive = false,
            MissionCommandId = string.Empty,
            MissionType = string.Empty,
            MissionPriority = 0,
            MissionDeadlineTicks = 0,
            MissionCondition = string.Empty,
            MissionLoops = true,
            AmmoPolicy = "auto",
            AmmoOverride = string.Empty,
            FailureAction = "report",
            FailureTarget = string.Empty,
            Moving = false,
            TargetX = 0,
            TargetY = 0,
            Stuck = false,
        };

    public static IntelRecordState Intel(
        string targetUnitId,
        IntelTier tier,
        ulong lastSeenTick,
        ulong memoryUntilTick,
        double x,
        double y,
        string sourceKind = "direct",
        string sourceUnitId = "friendly-1",
        double motionDx = 0,
        double motionDy = 0,
        ulong? observedCount = null,
        string? typeName = null,
        string? composition = null) =>
        new(
            "node-player",
            targetUnitId,
            tier,
            lastSeenTick,
            memoryUntilTick,
            1000,
            new IntelSourceState(sourceKind, sourceUnitId, "node-player", lastSeenTick),
            x,
            y,
            motionDx,
            motionDy,
            observedCount,
            typeName,
            composition);
}
