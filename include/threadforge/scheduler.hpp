#pragma once
#include "threadforge/task.hpp"
#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace threadforge {
enum class SchedulingPolicy { Fifo, Priority, WorkStealing };
// Caller supplies synchronization. Stable FIFO within each priority bucket.
class Scheduler {
public:
    explicit Scheduler(SchedulingPolicy policy, std::size_t workers = 1);
    void push(Task&& task, std::optional<std::size_t> owner = std::nullopt);
    Task pop(std::size_t worker = 0);
    std::optional<Task> remove(const std::shared_ptr<detail::TaskControl>& control);
    bool empty() const noexcept { return size_ == 0; }
    std::size_t size() const noexcept { return size_; }
    std::size_t steals() const noexcept { return steals_; }
private:
    SchedulingPolicy policy_;
    std::array<std::deque<Task>, 3> queues_;
    std::vector<std::deque<Task>> local_;
    std::size_t next_submission_ = 0;
    std::size_t steals_ = 0;
    std::size_t size_ = 0;
};
}
