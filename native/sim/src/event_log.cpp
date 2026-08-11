// sim/src/event_log.cpp
//
// T015：事件日志基础设施实现。
//
// 实现策略：
// - 存储使用 std::vector 并始终保持 seq 升序（append 单调 + 驱逐只删头部
//   或最旧非关键），保留集合等价于环形缓冲的"最新 N 条（关键优先）"语义，
//   不需要物理环形索引即可获得 O(capacity) 有界的追加（满员驱逐时
//   find_if 扫描最旧非关键事件）与确定性的全量遍历。
// - 满员驱逐固定为"最旧非关键 → 最旧"：先扫描最旧的非关键事件删除；全部
//   为关键事件时才删除最旧事件。该策略保证关键事件优先存活，且所有事件都
//   会被接纳（保留集合始终达到容量）。
// - critical_count_ 增量维护，避免快照摘要与查询反复扫描（T016 快照会
//   每 tick 读取该值）。
// - 名称互转使用固定映射表（存档序列化依赖稳定名称）；未知名称抛
//   std::invalid_argument，损坏存档显式报错而非静默恢复（宪法第 17 条）。

#include "wfs/sim/event_log.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace wfs::sim {

namespace {

constexpr std::size_t kCategoryCount = static_cast<std::size_t>(EventCategory::kCount);
constexpr std::size_t kSeverityCount = static_cast<std::size_t>(EventSeverity::kCount);

// 枚举越界立即显式报错（宪法第 17 条）：损坏的 category/severity 不应
// 进入日志后才在序列化阶段失败（fail-late）。
void ValidateEventEnums(EventCategory category, EventSeverity severity) {
    if (static_cast<std::size_t>(category) >= kCategoryCount || static_cast<std::size_t>(severity) >= kSeverityCount) {
        throw std::invalid_argument("wfs::sim::EventLog: unknown event category or severity");
    }
}

// 稳定名称表：顺序与枚举值一一对应，存档/日志输出不得变更。
constexpr std::array<std::string_view, static_cast<std::size_t>(EventCategory::kCount)> kCategoryNames = {
    "command", "combat", "intel", "mission", "logistics", "system",
};

constexpr std::array<std::string_view, static_cast<std::size_t>(EventSeverity::kCount)> kSeverityNames = {
    "debug",
    "info",
    "warning",
    "critical",
};

template <typename T, std::size_t N>
T FromName(const std::array<std::string_view, N>& names, std::string_view name, const char* type_name) {
    for (std::size_t i = 0U; i < names.size(); ++i) {
        if (names[i] == name) {
            return static_cast<T>(i);
        }
    }
    throw std::invalid_argument(std::string("wfs::sim: unknown ") + type_name + ": " + std::string(name));
}

}  // namespace

EventLog::EventLog(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0U) {
        throw std::invalid_argument("wfs::sim::EventLog: capacity must be >= 1");
    }
}

std::size_t EventLog::capacity() const noexcept {
    return capacity_;
}

std::size_t EventLog::size() const noexcept {
    return events_.size();
}

bool EventLog::empty() const noexcept {
    return events_.empty();
}

std::uint64_t EventLog::next_seq() const noexcept {
    return next_seq_;
}

std::size_t EventLog::critical_count() const noexcept {
    return critical_count_;
}

void EventLog::append(GameTick tick, EventCategory category, EventSeverity severity, std::string message) {
    append(tick, category, severity, std::move(message), next_seq_);
}

void EventLog::append(GameTick tick, EventCategory category, EventSeverity severity, std::string message,
                      std::uint64_t seq) {
    ValidateEventEnums(category, severity);
    if (seq < next_seq_) {
        throw std::invalid_argument("wfs::sim::EventLog: explicit seq must be monotonic (seq < next_seq)");
    }
    insert(SimEvent{tick, seq, category, severity, std::move(message)});
    next_seq_ = seq + 1U;
}

void EventLog::insert(SimEvent event) {
    if (events_.size() == capacity_) {
        // 满员驱逐：优先删除最旧非关键事件；全部为关键事件时删除最旧事件。
        const auto victim = std::find_if(events_.begin(), events_.end(), [](const SimEvent& retained) {
            return retained.severity != EventSeverity::kCritical;
        });
        const auto position = (victim != events_.end()) ? victim : events_.begin();
        if (position->severity == EventSeverity::kCritical) {
            --critical_count_;
        }
        events_.erase(position);
    }
    if (event.severity == EventSeverity::kCritical) {
        ++critical_count_;
    }
    events_.push_back(std::move(event));
}

std::vector<SimEvent> EventLog::events() const {
    return events_;
}

std::vector<SimEvent> EventLog::query(const EventFilter& filter) const {
    std::vector<SimEvent> result;
    result.reserve(std::min(events_.size(), filter.limit == 0U ? events_.size() : filter.limit));
    for (const SimEvent& event : events_) {
        if (filter.category.has_value() && event.category != *filter.category) {
            continue;
        }
        if (filter.min_severity.has_value() && event.severity < *filter.min_severity) {
            continue;
        }
        if (!filter.text.empty() && event.message.find(filter.text) == std::string::npos) {
            continue;
        }
        result.push_back(event);
        if (filter.limit != 0U && result.size() >= filter.limit) {
            break;
        }
    }
    return result;
}

std::string_view to_string(EventCategory category) noexcept {
    const std::size_t index = static_cast<std::size_t>(category);
    return (index < kCategoryNames.size()) ? kCategoryNames[index] : std::string_view{};
}

std::string_view to_string(EventSeverity severity) noexcept {
    const std::size_t index = static_cast<std::size_t>(severity);
    return (index < kSeverityNames.size()) ? kSeverityNames[index] : std::string_view{};
}

EventCategory event_category_from_string(std::string_view name) {
    return FromName<EventCategory>(kCategoryNames, name, "event category");
}

EventSeverity event_severity_from_string(std::string_view name) {
    return FromName<EventSeverity>(kSeverityNames, name, "event severity");
}

}  // namespace wfs::sim
