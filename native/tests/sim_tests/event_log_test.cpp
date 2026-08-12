// tests/sim_tests/event_log_test.cpp
//
// T015 单元测试：事件日志基础设施。
// 覆盖环形保留（默认 5000）、关键事件优先驱逐、严重级/分类过滤、文本搜索、
// 查询顺序确定性、显式 seq 恢复接口与名称互转。
// 全部断言只依赖显式 append 序列，不依赖任何现实时钟（宪法第 7/16 条）。

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wfs/sim/event_log.h"

namespace {

using wfs::sim::event_category_from_string;
using wfs::sim::event_severity_from_string;
using wfs::sim::EventCategory;
using wfs::sim::EventFilter;
using wfs::sim::EventLog;
using wfs::sim::EventSeverity;
using wfs::sim::SimEvent;
using wfs::sim::to_string;

constexpr EventCategory kCat = EventCategory::kCommand;
constexpr EventSeverity kInfo = EventSeverity::kInfo;

std::vector<std::uint64_t> Seqs(const std::vector<SimEvent>& events) {
    std::vector<std::uint64_t> seqs;
    seqs.reserve(events.size());
    for (const SimEvent& event : events) {
        seqs.push_back(event.seq);
    }
    return seqs;
}

std::vector<std::string> Messages(const std::vector<SimEvent>& events) {
    std::vector<std::string> messages;
    messages.reserve(events.size());
    for (const SimEvent& event : events) {
        messages.push_back(event.message);
    }
    return messages;
}

}  // namespace

TEST(WfsEventLogTest, AppendsInSeqOrderAndReportsState) {
    EventLog log;
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0u);
    EXPECT_EQ(log.capacity(), 5000u);
    EXPECT_EQ(log.next_seq(), 0u);

    log.append(3u, kCat, kInfo, "third");
    log.append(1u, kCat, kInfo, "first");
    log.append(2u, kCat, kInfo, "second");

    EXPECT_FALSE(log.empty());
    EXPECT_EQ(log.size(), 3u);
    EXPECT_EQ(log.next_seq(), 3u);
    EXPECT_EQ(Seqs(log.events()), (std::vector<std::uint64_t>{0u, 1u, 2u}));
    EXPECT_EQ(Messages(log.events()), (std::vector<std::string>{"third", "first", "second"}));
    // tick 由调用方显式传入，日志本身不排序 tick（顺序只由 seq 决定）。
    EXPECT_EQ(log.events()[0].tick, 3u);
    EXPECT_EQ(log.events()[2].tick, 2u);
}

TEST(WfsEventLogTest, RingRetentionKeepsNewestDefaultCapacity) {
    EventLog log;  // 默认 5000 条环形保留。
    for (std::uint64_t i = 0u; i < 5001u; ++i) {
        log.append(i, kCat, kInfo, "m" + std::to_string(i));
    }
    EXPECT_EQ(log.size(), 5000u);
    EXPECT_EQ(log.next_seq(), 5001u);
    // 最旧的 seq=0 被驱逐，保留 1..5000。
    EXPECT_EQ(Seqs(log.events()).front(), 1u);
    EXPECT_EQ(Seqs(log.events()).back(), 5000u);
    EXPECT_EQ(log.events().front().message, "m1");
}

TEST(WfsEventLogTest, RingRetentionKeepsNewestCustomCapacity) {
    EventLog log(3u);
    for (std::uint64_t i = 0u; i < 5u; ++i) {
        log.append(i, kCat, kInfo, "m" + std::to_string(i));
    }
    EXPECT_EQ(log.size(), 3u);
    EXPECT_EQ(Seqs(log.events()), (std::vector<std::uint64_t>{2u, 3u, 4u}));
}

TEST(WfsEventLogTest, CriticalEventsSurviveNonCriticalFlood) {
    EventLog log(5u);
    log.append(0u, EventCategory::kSystem, EventSeverity::kCritical, "critical-0");
    for (std::uint64_t i = 1u; i <= 4u; ++i) {
        log.append(i, kCat, kInfo, "normal-" + std::to_string(i));
    }
    log.append(5u, kCat, kInfo, "normal-5");
    log.append(6u, EventCategory::kCombat, EventSeverity::kCritical, "critical-6");
    log.append(7u, kCat, kInfo, "normal-7");

    // 驱逐总是先选最旧的非关键事件，关键事件必须存活。
    EXPECT_EQ(log.size(), 5u);
    EXPECT_EQ(Seqs(log.events()), (std::vector<std::uint64_t>{0u, 4u, 5u, 6u, 7u}));
    EXPECT_EQ(log.critical_count(), 2u);
    const std::vector<SimEvent> critical = log.query(EventFilter{std::nullopt, EventSeverity::kCritical, "", 0u});
    EXPECT_EQ(Seqs(critical), (std::vector<std::uint64_t>{0u, 6u}));
}

TEST(WfsEventLogTest, CriticalOnlyBufferEvictsOldest) {
    EventLog log(3u);
    log.append(0u, kCat, EventSeverity::kCritical, "c0");
    log.append(1u, kCat, EventSeverity::kCritical, "c1");
    log.append(2u, kCat, EventSeverity::kCritical, "c2");
    // 全部为关键事件时，新非关键事件只能驱逐最旧（保持环形 FIFO）。
    log.append(3u, kCat, kInfo, "n3");
    EXPECT_EQ(Seqs(log.events()), (std::vector<std::uint64_t>{1u, 2u, 3u}));
    // 之后新关键事件驱逐最旧非关键，而不是关键事件。
    log.append(4u, kCat, EventSeverity::kCritical, "c4");
    EXPECT_EQ(Seqs(log.events()), (std::vector<std::uint64_t>{1u, 2u, 4u}));
    EXPECT_EQ(log.critical_count(), 3u);
}

TEST(WfsEventLogTest, FiltersBySeverity) {
    EventLog log;
    log.append(0u, kCat, EventSeverity::kDebug, "debug");
    log.append(1u, kCat, EventSeverity::kInfo, "info");
    log.append(2u, kCat, EventSeverity::kWarning, "warning");
    log.append(3u, kCat, EventSeverity::kCritical, "critical");

    const std::vector<SimEvent> warnings_up = log.query(EventFilter{std::nullopt, EventSeverity::kWarning, "", 0u});
    EXPECT_EQ(Seqs(warnings_up), (std::vector<std::uint64_t>{2u, 3u}));

    const std::vector<SimEvent> all = log.query(EventFilter{std::nullopt, std::nullopt, "", 0u});
    EXPECT_EQ(all.size(), 4u);
}

TEST(WfsEventLogTest, FiltersByCategory) {
    EventLog log;
    log.append(0u, EventCategory::kCombat, kInfo, "hit");
    log.append(1u, kCat, kInfo, "command");
    log.append(2u, EventCategory::kCombat, kInfo, "kill");
    log.append(3u, EventCategory::kIntel, kInfo, "contact");

    const std::vector<SimEvent> combat = log.query(EventFilter{EventCategory::kCombat, std::nullopt, "", 0u});
    EXPECT_EQ(Seqs(combat), (std::vector<std::uint64_t>{0u, 2u}));
}

TEST(WfsEventLogTest, SearchesByTextSubstring) {
    EventLog log;
    log.append(0u, kCat, kInfo, "unit alpha spotted");
    log.append(1u, kCat, kInfo, "unit bravo engaged");
    log.append(2u, kCat, kInfo, "ALPHA uppercase");

    const std::vector<SimEvent> matches = log.query(EventFilter{std::nullopt, std::nullopt, "alpha", 0u});
    // 区分大小写子串匹配：只命中 seq=0。
    EXPECT_EQ(Seqs(matches), (std::vector<std::uint64_t>{0u}));
}

TEST(WfsEventLogTest, QueryLimitAndComposedFilters) {
    EventLog log;
    for (std::uint64_t i = 0u; i < 10u; ++i) {
        log.append(i, EventCategory::kMission, EventSeverity::kInfo, "task " + std::to_string(i));
    }
    const std::vector<SimEvent> limited =
        log.query(EventFilter{EventCategory::kMission, EventSeverity::kInfo, "task", 3u});
    EXPECT_EQ(Seqs(limited), (std::vector<std::uint64_t>{0u, 1u, 2u}));
}

TEST(WfsEventLogTest, QueryOnEmptyLogReturnsEmpty) {
    const EventLog log;
    EXPECT_TRUE(log.query(EventFilter{EventCategory::kCombat, EventSeverity::kCritical, "x", 0u}).empty());
}

TEST(WfsEventLogTest, SameAppendSequenceProducesSameLog) {
    EventLog first;
    EventLog second;
    for (std::uint64_t i = 0u; i < 37u; ++i) {
        const EventCategory category = (i % 3u == 0u) ? EventCategory::kCombat : EventCategory::kMission;
        const EventSeverity severity = (i % 5u == 0u) ? EventSeverity::kCritical : EventSeverity::kInfo;
        first.append(i, category, severity, "event " + std::to_string(i));
        second.append(i, category, severity, "event " + std::to_string(i));
    }
    EXPECT_EQ(first.events(), second.events());
    EXPECT_EQ(first.query(EventFilter{std::nullopt, EventSeverity::kCritical, "event", 0u}),
              second.query(EventFilter{std::nullopt, EventSeverity::kCritical, "event", 0u}));
}

TEST(WfsEventLogTest, ZeroCapacityRejected) {
    EXPECT_THROW(EventLog(0u), std::invalid_argument);
}

TEST(WfsEventLogTest, ExplicitSeqRestorePath) {
    EventLog log;
    // 存档恢复路径：显式 seq 必须单调（>= next_seq）。
    log.append(10u, EventCategory::kSystem, EventSeverity::kInfo, "restored-1000", 1000u);
    EXPECT_EQ(log.next_seq(), 1001u);
    log.append(11u, EventCategory::kSystem, EventSeverity::kInfo, "restored-1005", 1005u);
    EXPECT_EQ(log.next_seq(), 1006u);
    EXPECT_EQ(Seqs(log.events()), (std::vector<std::uint64_t>{1000u, 1005u}));

    // 重复/回退 seq 必须显式报错且不改变日志。
    EXPECT_THROW(log.append(12u, EventCategory::kSystem, EventSeverity::kInfo, "dup", 1005u), std::invalid_argument);
    EXPECT_THROW(log.append(13u, EventCategory::kSystem, EventSeverity::kInfo, "past", 999u), std::invalid_argument);
    EXPECT_EQ(log.size(), 2u);
    EXPECT_EQ(log.next_seq(), 1006u);

    // 显式 seq 之后 auto 从 max+1 继续。
    log.append(14u, EventCategory::kSystem, EventSeverity::kInfo, "auto");
    EXPECT_EQ(Seqs(log.events()).back(), 1006u);
}

TEST(WfsEventLogTest, CategoryAndSeverityNameRoundTrip) {
    EXPECT_EQ(to_string(EventCategory::kCommand), "command");
    EXPECT_EQ(to_string(EventCategory::kCombat), "combat");
    EXPECT_EQ(to_string(EventCategory::kIntel), "intel");
    EXPECT_EQ(to_string(EventCategory::kMission), "mission");
    EXPECT_EQ(to_string(EventCategory::kLogistics), "logistics");
    EXPECT_EQ(to_string(EventCategory::kSystem), "system");

    EXPECT_EQ(to_string(EventSeverity::kDebug), "debug");
    EXPECT_EQ(to_string(EventSeverity::kInfo), "info");
    EXPECT_EQ(to_string(EventSeverity::kWarning), "warning");
    EXPECT_EQ(to_string(EventSeverity::kCritical), "critical");

    EXPECT_EQ(event_category_from_string("combat"), EventCategory::kCombat);
    EXPECT_EQ(event_severity_from_string("critical"), EventSeverity::kCritical);
    EXPECT_THROW(event_category_from_string("unknown"), std::invalid_argument);
    EXPECT_THROW(event_severity_from_string(""), std::invalid_argument);
}
