#include "threadforge/scheduler.hpp"
#include <stdexcept>
#include <utility>
namespace threadforge {
Scheduler::Scheduler(SchedulingPolicy policy, std::size_t workers) : policy_(policy) {
    if (policy != SchedulingPolicy::Fifo && policy != SchedulingPolicy::Priority && policy != SchedulingPolicy::WorkStealing) {
        throw std::invalid_argument("Invalid scheduling policy");
    }
    if (workers == 0) { throw std::invalid_argument("Scheduler requires workers"); }
    if (policy == SchedulingPolicy::WorkStealing) { local_.resize(workers); }
}
void Scheduler::push(Task&& task, std::optional<std::size_t> owner) {
    const auto priority = static_cast<std::size_t>(task.priority);
    if (priority >= queues_.size()) { throw std::invalid_argument("Invalid priority"); }
    if (policy_ == SchedulingPolicy::WorkStealing) {
        const auto destination = owner.value_or(next_submission_);
        if (destination >= local_.size()) { throw std::invalid_argument("Invalid worker index"); }
        local_[destination].push_back(std::move(task));
        if (!owner) { next_submission_ = (next_submission_ + 1) % local_.size(); }
    } else {
        queues_[policy_ == SchedulingPolicy::Fifo ? 0 : priority].push_back(std::move(task));
    }
    ++size_;
}
Task Scheduler::pop(std::size_t worker) {
    if (policy_ == SchedulingPolicy::WorkStealing) {
        if (worker >= local_.size()) { throw std::invalid_argument("Invalid worker index"); }
        if (!local_[worker].empty()) {
            Task task = std::move(local_[worker].back());
            local_[worker].pop_back();
            --size_;
            return task;
        }
        // Deterministic cyclic victim scan; steal the victim's oldest task.
        for (std::size_t offset = 1; offset < local_.size(); ++offset) {
            auto& victim = local_[(worker + offset) % local_.size()];
            if (!victim.empty()) {
                Task task = std::move(victim.front());
                victim.pop_front();
                --size_;
                ++steals_;
                return task;
            }
        }
        throw std::logic_error("Cannot pop an empty scheduler");
    }
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
    auto remove_from = [&](auto& queues) -> std::optional<Task> {
      for (auto& queue : queues) {
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
    };
    return policy_ == SchedulingPolicy::WorkStealing ? remove_from(local_) : remove_from(queues_);
}
}
