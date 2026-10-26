#pragma once
#include "threadforge/task.hpp"
#include <array>
#include <cstddef>
#include <deque>
#include <optional>

namespace threadforge {
enum class SchedulingPolicy { Fifo, Priority };
// Caller supplies synchronization. Stable FIFO within each priority bucket.
class Scheduler {
public:
    explicit Scheduler(SchedulingPolicy policy);
    void push(Task&& task);
    Task pop();
    std::optional<Task> remove(const std::shared_ptr<detail::TaskControl>& control);
    bool empty() const noexcept { return size_ == 0; }
    std::size_t size() const noexcept { return size_; }
private:
    SchedulingPolicy policy_;
    std::array<std::deque<Task>, 3> queues_;
    std::size_t size_ = 0;
};
}
