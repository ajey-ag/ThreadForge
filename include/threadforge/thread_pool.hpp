#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace threadforge {

// Destruction must not overlap any public call and must not occur on a worker.
class ThreadPool final {
public:
    explicit ThreadPool(std::size_t worker_count);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Owns a copyable void() callable. Throws for empty tasks or closed pool.
    void submit(std::function<void()> task);
    // Idempotent, concurrent external calls supported. Workers may not call it.
    void shutdown();
    // Phase 1 exception containment; asynchronous results arrive in Phase 2.
    std::size_t failed_task_count() const noexcept;

private:
    enum class State { Running, Draining, Stopped };
    void worker_loop();
    static thread_local ThreadPool* current_pool_;

    std::mutex queue_mutex_;
    std::condition_variable work_available_;
    std::queue<std::function<void()>> tasks_;
    State state_ = State::Running;
    std::mutex shutdown_mutex_;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> failed_tasks_{0};
};

} // namespace threadforge
