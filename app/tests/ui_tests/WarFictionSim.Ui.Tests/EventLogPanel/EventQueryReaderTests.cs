// 测试：事件日志 —— wfs_sim_query_events 响应解析（FR-044 数据通道）。
//
// 响应契约（contracts/sim-c-api.md）：events 按 seq 升序、count 为截断前
// 总数、truncated 标记 limit 截断；未知分类/严重级名称与非法 JSON 显式报错
// （宪法第 17 条），不让脏数据静默进入面板。

using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.EventLogPanel;

public class EventQueryReaderTests
{
    private const string ResponseJson = """
        {
          "events": [
            {"seq": 1, "tick": 20, "category": "combat", "severity": "critical", "message": "接敌"},
            {"seq": 2, "tick": 21, "category": "command", "severity": "info", "message": "COMMAND_QUEUED"}
          ],
          "count": 42,
          "truncated": true
        }
        """;

    [Fact]
    public void Parse_ValidResponse_ReadsEventsCountAndTruncated()
    {
        EventQueryResponse response = EventQueryReader.Parse(ResponseJson);

        Assert.Equal((ulong)42, response.Count);
        Assert.True(response.Truncated);
        Assert.Equal([1uL, 2uL], response.Events.Select(item => item.Seq));
        Assert.Equal(SimEventSeverity.Critical, response.Events[0].Severity);
        Assert.Equal("接敌", response.Events[0].Message);
    }

    [Fact]
    public void Parse_UnknownCategory_ThrowsParseException()
    {
        string json = ResponseJson.Replace("\"combat\"", "\"teleport\"", StringComparison.Ordinal);

        SnapshotParseException exception = Assert.Throws<SnapshotParseException>(() => EventQueryReader.Parse(json));

        Assert.Contains("teleport", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Parse_InvalidJson_ThrowsParseException()
    {
        SnapshotParseException exception =
            Assert.Throws<SnapshotParseException>(() => EventQueryReader.Parse("{ 不是 JSON"));

        Assert.Contains("事件查询", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Parse_MissingCount_ThrowsParseException()
    {
        string json = """{ "events": [], "truncated": false }""";

        Assert.Throws<SnapshotParseException>(() => EventQueryReader.Parse(json));
    }
}
