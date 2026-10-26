#include "threadforge/thread_pool.hpp"

#include <stdexcept>
#include <utility>

namespace threadforge {

thread_local ThreadPool* ThreadPool::current_pool_ = nullptr;

ThreadPool::ThreadPool(std::size_t worker_count) {
    if (worker_count == 0) {
        throw std::invalid_argument("ThreadPool requires at least one worker");
    }
    workers_.reserve(worker_count);
    try {
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        // A failed constructor has no destructor: explicitly join created workers.
        shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    if (!task) {
        throw std::invalid_argument("Cannot submit an empty task");
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (state_ != State::Running) {
            throw std::runtime_error("ThreadPool is shutting down");
        }
        tasks_.push(std::move(task));
    }
    work_available_.notify_one();
}

void ThreadPool::shutdown() {
    if (current_pool_ == this) {
        throw std::logic_error("A worker cannot join its own pool");
    }
    // Serializes joins. Workers never acquire this mutex.
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (state_ == State::Stopped) {
            return;
        }
        state_ = State::Draining;
    }
    work_available_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        state_ = State::Stopped;
    }
}

std::size_t ThreadPool::failed_task_count() const noexcept {
    return failed_tasks_.load(std::memory_order_relaxed);
}

void ThreadPool::worker_loop() {
    current_pool_ = this;
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            work_available_.wait(lock, [this] {
                return state_ != State::Running || !tasks_.empty();
            });
            if (tasks_.empty()) {
                break; // Predicate guarantees shutdown has started.
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (...) {
            failed_tasks_.fetch_add(1, std::memory_order_relaxed);
        }
        // Callable and captured resources are destroyed outside the queue lock.
    }
    current_pool_ = nullptr;
}

} // namespace threadforge
