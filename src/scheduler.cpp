#include "threadforge/scheduler.hpp"
#include <stdexcept>
#include <utility>
namespace threadforge {
Scheduler::Scheduler(SchedulingPolicy policy) : policy_(policy) {
    if (policy != SchedulingPolicy::Fifo && policy != SchedulingPolicy::Priority) {
        throw std::invalid_argument("Invalid scheduling policy");
    }
}
void Scheduler::push(Task&& task) {
    const auto priority = static_cast<std::size_t>(task.priority);
    if (priority >= queues_.size()) { throw std::invalid_argument("Invalid priority"); }
    queues_[policy_ == SchedulingPolicy::Fifo ? 0 : priority].push_back(std::move(task));
    ++size_;
}
Task Scheduler::pop() {
    for (auto& queue : queues_) {
        if (!queue.empty()) {
            Task task = std::move(queue.front());
            queue.pop_front();
            --size_;
            return task;
        }
    }
    throw std::logic_error("Cannot pop an empty scheduler");
}
std::optional<Task> Scheduler::remove(const std::shared_ptr<detail::TaskControl>& control) {
    for (auto& queue : queues_) {
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->control == control) {
                Task task = std::move(*it);
                queue.erase(it);
                --size_;
                return task;
            }
        }
    }
    return std::nullopt;
}
}
