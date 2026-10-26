#include "threadforge/thread_pool.hpp"
#include <stdexcept>
#include <utility>

namespace threadforge {
thread_local ThreadPool* ThreadPool::current_pool_ = nullptr;

ThreadPool::ThreadPool(std::size_t workers, SchedulingPolicy policy, std::size_t capacity)
    : state_(std::make_shared<detail::PoolState>(policy)), queue_capacity_(capacity) {
    if (workers == 0) { throw std::invalid_argument("ThreadPool requires at least one worker"); }
    workers_.reserve(workers);
    try {
        for (std::size_t i = 0; i < workers; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        shutdown();
        throw;
    }
}
ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::enqueue(Task task) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->lifecycle != detail::Lifecycle::Running) { throw PoolClosed(); }
        if (queue_capacity_ != 0 && state_->tasks.size() >= queue_capacity_) { throw QueueFull(); }
        state_->tasks.push(std::move(task));
    }
    state_->available.notify_one();
}

bool TaskHandle::cancel() const {
    auto pool = pool_.lock();
    if (!pool) { return false; }
    std::optional<Task> removed;
    {
        std::lock_guard<std::mutex> lock(pool->mutex);
        if (control_->state.load(std::memory_order_relaxed) != TaskState::Queued) { return false; }
        removed = pool->tasks.remove(control_);
        if (!removed) { return false; }
        control_->state.store(TaskState::Cancelled, std::memory_order_release);
        ++pool->active_cancellations;
    }
    removed->execute(); // Resolves cancellation; never invokes the user's callable.
    {
        std::lock_guard<std::mutex> lock(pool->mutex);
        --pool->active_cancellations;
    }
    pool->available.notify_all();
    return true;
}

void ThreadPool::shutdown(ShutdownMode mode) {
    if (current_pool_ == this) { throw std::logic_error("A worker cannot join its own pool"); }
    if (mode != ShutdownMode::Graceful && mode != ShutdownMode::CancelPending) {
        throw std::invalid_argument("Invalid shutdown mode");
    }
    // Declared before the lock: captured user resources are destroyed after unlocking.
    std::vector<Task> cancelled;
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->lifecycle == detail::Lifecycle::Stopped) { return; }
        if (mode == ShutdownMode::CancelPending) { cancelled.reserve(state_->tasks.size()); }
        state_->lifecycle = detail::Lifecycle::Draining;
        if (mode == ShutdownMode::CancelPending) {
            while (!state_->tasks.empty()) {
                auto task = state_->tasks.pop();
                task.control->state.store(TaskState::Cancelled, std::memory_order_release);
                cancelled.push_back(std::move(task));
            }
        }
    }
    state_->available.notify_all();
    for (auto& task : cancelled) { task.execute(); }
    for (auto& worker : workers_) { worker.join(); }
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->available.wait(lock, [this] { return state_->active_cancellations == 0; });
        state_->lifecycle = detail::Lifecycle::Stopped;
    }
}

void ThreadPool::worker_loop() {
    current_pool_ = this;
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->available.wait(lock, [this] {
                return state_->lifecycle != detail::Lifecycle::Running || !state_->tasks.empty();
            });
            if (state_->tasks.empty()) { break; }
            task = state_->tasks.pop();
            task.control->state.store(TaskState::Running, std::memory_order_release);
        }
        task.execute();
        if (task.control->state.load(std::memory_order_acquire) == TaskState::Failed) {
            state_->failed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    current_pool_ = nullptr;
}
}
