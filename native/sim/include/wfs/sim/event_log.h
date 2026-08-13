// sim/include/wfs/sim/event_log.h
//
// T015：事件日志基础设施公开接口。
//
// 设计契约（data-model.md §17；宪法第 7/16 条）：
// - 事件按 分类/严重级 结构化记录，调用方显式传入游戏 tick；本类型不读取
//   任何现实时钟，seq 由 append 顺序单调分配，因此同一调用序列必然产生
//   同一日志内容（确定性，宪法第 7 条）。
// - 环形保留：默认容量 5000（kDefaultCapacity），满员时按"最旧非关键 →
//   最旧"的固定策略驱逐，保证关键事件优先存活——只要日志里还存在非关键
//   事件，新事件就不会驱逐关键事件；全部为关键事件时才驱逐最旧事件。
//   驱逐策略与容量都只影响保留集合，不影响 seq 的单调分配。
// - 过滤与搜索：query() 按固定顺序（seq 升序）返回，过滤条件可组合
//   （分类 / 最低严重级 / 文本子串），limit 截断；同一日志与同一过滤器
//   必然产生同一结果。
// - 显式 seq 追加接口专供存档恢复（T017）：要求 seq >= next_seq()，
//   保持全局单调、天然防重复；非法调用抛 std::invalid_argument（宪法
//   第 17 条：显式报错，不静默吞错）。seq == UINT64_MAX 时游标饱和停在
//   MAX（与 EventQueue 一致，不回绕）；空间耗尽后 auto 追加抛
//   std::overflow_error。
// - 类别/严重级与字符串的互转使用稳定名称，供存档序列化（save-format.md：
//   state_blob 完整可序列化）；未知名称抛 std::invalid_argument，损坏数据
//   报错而非静默恢复。

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wfs/sim/clock.h"

namespace wfs::sim {

// 事件分类：稳定枚举值用于过滤与序列化；名称映射见 to_string/from_string。
enum class EventCategory : std::uint8_t {
    kCommand = 0,    // 命令链路（下达/确认/执行）。
    kCombat = 1,     // 战斗结算（命中/伤害/压制/失联）。
    kIntel = 2,      // 情报（发现/识别/同步）。
    kMission = 3,    // 任务（完成/失败/超时/取消）。
    kLogistics = 4,  // 后勤（补给/后送/维修）。
    kSystem = 5,     // 系统（存档/校验/切换）。
    kCount = 6,      // 枚举数量哨兵，不表示具体分类。
};

// 事件严重级：过滤与保留优先级共用该排序（kDebug < kInfo < kWarning < kCritical）。
enum class EventSeverity : std::uint8_t {
    kDebug = 0,
    kInfo = 1,
    kWarning = 2,
    kCritical = 3,
    kCount = 4,  // 枚举数量哨兵，不表示具体严重级。
};

struct SimEvent {
    GameTick tick;      // 事件发生的游戏 tick（显式传入，不读现实时钟）。
    std::uint64_t seq;  // 全局单调序号（append 顺序）。
    EventCategory category;
    EventSeverity severity;
    std::string message;

    bool operator==(const SimEvent&) const = default;
};

// 查询过滤器：空字段表示不过滤；text 为区分大小写的子串匹配。
struct EventFilter {
    std::optional<EventCategory> category;      // nullopt = 全部类别。
    std::optional<EventSeverity> min_severity;  // nullopt = 全部严重级。
    std::string text;                           // 空 = 全部消息。
    std::size_t limit = 0U;                     // 0 = 不限制数量。
};

class EventLog {
   public:
    static constexpr std::size_t kDefaultCapacity = 5000U;

    // capacity 为保留上限，必须 >= 1，否则抛 std::invalid_argument。
    explicit EventLog(std::size_t capacity = kDefaultCapacity);

    std::size_t capacity() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    std::uint64_t next_seq() const noexcept;
    // 当前保留中 kCritical 事件数（快照摘要与监控用，O(1)）。
    std::size_t critical_count() const noexcept;

    // 追加事件：seq 自动分配；容量满时按"最旧非关键 → 最旧"驱逐。
    void append(GameTick tick, EventCategory category, EventSeverity severity, std::string message);
    // 追加事件：显式 seq（存档恢复用），要求 seq >= next_seq()。
    void append(GameTick tick, EventCategory category, EventSeverity severity, std::string message, std::uint64_t seq);

    // 全部保留事件，按 seq 升序（追加顺序）。
    std::vector<SimEvent> events() const;
    // 过滤 + 搜索，按 seq 升序返回。
    std::vector<SimEvent> query(const EventFilter& filter) const;

   private:
    void insert(SimEvent event);

    std::size_t capacity_;
    std::vector<SimEvent> events_;  // 始终按 seq 升序（追加有序 + 驱逐有序）。
    std::uint64_t next_seq_ = 0U;
    std::size_t critical_count_ = 0U;
};

// 稳定名称（存档/日志输出用）：command/combat/intel/mission/logistics/system。
std::string_view to_string(EventCategory category) noexcept;
// 稳定名称：debug/info/warning/critical。
std::string_view to_string(EventSeverity severity) noexcept;

// 名称 → 枚举；未知名称抛 std::invalid_argument（损坏存档显式报错）。
EventCategory event_category_from_string(std::string_view name);
EventSeverity event_severity_from_string(std::string_view name);

}  // namespace wfs::sim
