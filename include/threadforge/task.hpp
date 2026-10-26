#pragma once
#include <functional>
#include <atomic>
#include <memory>
#include <future>
#include <stdexcept>
#include <utility>
#include <chrono>
namespace threadforge {
enum class Priority { High = 0, Normal = 1, Low = 2 };
enum class TaskState { Queued, Running, Completed, Failed, Cancelled };
namespace detail {
struct PoolState;
struct TaskControl { std::atomic<TaskState> state{TaskState::Queued}; };
}
class ThreadPool;
class TaskHandle {
public:
    bool cancel() const;
    TaskState state() const {
        if (!control_) { throw std::logic_error("Moved-from task handle"); }
        return control_->state.load(std::memory_order_acquire);
    }
private:
    friend class ThreadPool;
    TaskHandle(std::weak_ptr<detail::PoolState> pool, std::shared_ptr<detail::TaskControl> control)
        : pool_(std::move(pool)), control_(std::move(control)) {}
    std::weak_ptr<detail::PoolState> pool_;
    std::shared_ptr<detail::TaskControl> control_;
};
template <class R> struct Submission {
    std::future<R> future;
    TaskHandle handle;
};
struct Task {
    std::function<void()> execute;
    Priority priority = Priority::Normal;
    std::shared_ptr<detail::TaskControl> control;
    std::chrono::steady_clock::time_point enqueued{};
};
}
