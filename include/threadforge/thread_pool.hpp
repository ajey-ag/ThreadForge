#pragma once
#include "threadforge/errors.hpp"
#include "threadforge/scheduler.hpp"
#include "threadforge/metrics.hpp"
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadforge {
enum class ShutdownMode { Graceful, CancelPending };
namespace detail {
enum class Lifecycle { Running, Draining, Stopped };
struct PoolState {
    explicit PoolState(SchedulingPolicy policy, bool timing) : tasks(policy), timing_enabled(timing) {}
    std::mutex mutex;
    std::condition_variable available;
    Scheduler tasks;
    Lifecycle lifecycle = Lifecycle::Running;
    MetricsData metrics;
    const bool timing_enabled;
    std::size_t active_cancellations = 0;
};
}

// Destruction must not overlap public calls or occur on one of this pool's workers.
class ThreadPool final {
public:
    explicit ThreadPool(std::size_t workers, SchedulingPolicy policy = SchedulingPolicy::Fifo,
                        std::size_t queue_capacity = 0, bool timing_metrics = true);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <class F, class... Args>
    auto submit(F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        return submit_with_priority(Priority::Normal, std::forward<F>(function), std::forward<Args>(args)...);
    }
    template <class F, class... Args>
    auto submit_with_priority(Priority priority, F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        return submit_cancellable_with_priority(priority, std::forward<F>(function),
                                               std::forward<Args>(args)...).future;
    }
    template <class F, class... Args>
    auto submit_cancellable(F&& function, Args&&... args)
        -> Submission<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        return submit_cancellable_with_priority(Priority::Normal, std::forward<F>(function),
                                               std::forward<Args>(args)...);
    }
    template <class F, class... Args>
    auto submit_cancellable_with_priority(Priority priority, F&& function, Args&&... args)
        -> Submission<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        auto control = std::make_shared<detail::TaskControl>();
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        // Shared ownership makes the queue wrapper copyable even with move-only inputs.
        auto invocation = [function = std::forward<F>(function),
                           arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable -> R {
            return std::apply(std::move(function), std::move(arguments));
        };
        auto owned = std::make_shared<decltype(invocation)>(std::move(invocation));
        Task task{[control, promise, owned] {
            if (control->state.load(std::memory_order_acquire) == TaskState::Cancelled) {
                promise->set_exception(std::make_exception_ptr(TaskCancelled()));
                return;
            }
            try {
                if constexpr (std::is_void_v<R>) {
                    (*owned)();
                    promise->set_value();
                } else {
                    promise->set_value((*owned)());
                }
                control->state.store(TaskState::Completed, std::memory_order_release);
            } catch (...) {
                promise->set_exception(std::current_exception());
                control->state.store(TaskState::Failed, std::memory_order_release);
            }
        }, priority, control};
        enqueue(std::move(task));
        return {std::move(future), TaskHandle(state_, std::move(control))};
    }

    // First shutdown caller chooses the mode; subsequent callers wait for that shutdown.
    void shutdown(ShutdownMode mode = ShutdownMode::Graceful);
    Metrics metrics() const;
    std::size_t failed_task_count() const { return metrics().failed; }

private:
    void enqueue(Task task);
    void worker_loop();
    static thread_local ThreadPool* current_pool_;
    std::shared_ptr<detail::PoolState> state_;
    const std::size_t queue_capacity_;
    std::mutex shutdown_mutex_;
    std::vector<std::thread> workers_;
};
}
