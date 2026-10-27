#include "threadforge/thread_pool.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
template <class Exception, class F>
void expect_throw(F function) {
    bool caught = false;
    try { function(); } catch (const Exception&) { caught = true; }
    require(caught, "Expected exception was not thrown");
}

// A C++17 rendezvous. CTest's timeout catches failures without sleep-based tests.
class Gate {
public:
    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrivals_;
        changed_.notify_all();
        changed_.wait(lock, [this] { return open_; });
    }
    void wait_for(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this, count] { return arrivals_ >= count; });
    }
    void open() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
        changed_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t arrivals_ = 0;
    bool open_ = false;
};

void validation() {
    expect_throw<std::invalid_argument>([] { threadforge::ThreadPool pool(0); });
    threadforge::ThreadPool pool(1);
    auto empty = pool.submit(std::function<void()>{});
    expect_throw<std::bad_function_call>([&] { empty.get(); });
    int result = 0;
    pool.submit([&] { result = 42; });
    pool.shutdown();
    require(result == 42, "Single task did not execute");
    expect_throw<std::runtime_error>([&] { pool.submit([] {}); });
    pool.shutdown();
}

void fifo_and_destruction() {
    std::vector<int> order;
    {
        threadforge::ThreadPool pool(1);
        for (int i = 0; i < 100; ++i) {
            pool.submit([&, i] { order.push_back(i); });
        }
    }
    require(order.size() == 100, "Destructor lost tasks");
    for (int i = 0; i < 100; ++i) {
        require(order[static_cast<std::size_t>(i)] == i, "FIFO order violated");
    }
}

void concurrent_workers() {
    Gate gate;
    threadforge::ThreadPool pool(4);
    for (int i = 0; i < 4; ++i) { pool.submit([&] { gate.arrive_and_wait(); }); }
    gate.wait_for(4); // Cannot pass unless all four workers execute concurrently.
    gate.open();
    pool.shutdown();
}

void producers_exactly_once() {
    constexpr std::size_t count = 10000;
    std::array<std::atomic<unsigned>, count> hits{};
    for (auto& hit : hits) { hit.store(0); }
    threadforge::ThreadPool pool(4);
    std::vector<std::thread> producers;
    for (std::size_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            for (std::size_t i = producer; i < count; i += 4) {
                pool.submit([&, i] { hits[i].fetch_add(1); });
            }
        });
    }
    for (auto& producer : producers) { producer.join(); }
    pool.shutdown();
    for (const auto& hit : hits) { require(hit.load() == 1, "Lost or duplicate task"); }
}

void shutdown_race() {
    Gate start;
    std::atomic<unsigned> accepted{0};
    std::atomic<unsigned> executed{0};
    threadforge::ThreadPool pool(4);
    // Ensure there is accepted work regardless of which racer wins.
    pool.submit([&] { executed.fetch_add(1); });
    accepted.fetch_add(1);
    std::thread producer([&] {
        start.arrive_and_wait();
        for (int i = 0; i < 1000; ++i) {
            try {
                pool.submit([&] { executed.fetch_add(1); });
                accepted.fetch_add(1);
            } catch (const std::runtime_error&) { break; }
        }
    });
    std::thread closer1([&] { start.arrive_and_wait(); pool.shutdown(); });
    std::thread closer2([&] { start.arrive_and_wait(); pool.shutdown(); });
    start.wait_for(3);
    start.open();
    producer.join();
    closer1.join();
    closer2.join();
    require(accepted.load() == executed.load(), "Shutdown lost accepted work");
}

void exceptions_and_worker_shutdown() {
    threadforge::ThreadPool pool(1);
    bool rejected = false;
    bool continued = false;
    pool.submit([] { throw std::runtime_error("task failure"); });
    pool.submit([&] {
        try { pool.shutdown(); } catch (const std::logic_error&) { rejected = true; }
    });
    pool.submit([&] { continued = true; });
    pool.shutdown();
    require(rejected && continued, "Worker shutdown or exception containment failed");
    require(pool.failed_task_count() == 1, "Task exception was not counted");
}

void drain_running_and_queued() {
    Gate running;
    threadforge::ThreadPool pool(1);
    unsigned finished = 0;
    pool.submit([&] { running.arrive_and_wait(); ++finished; });
    running.wait_for(1);
    for (int i = 0; i < 100; ++i) { pool.submit([&] { ++finished; }); }
    std::thread closer([&] { pool.shutdown(); });
    running.open();
    closer.join();
    require(finished == 101, "Shutdown did not wait for running and queued work");
}

void idle_shutdown() {
    threadforge::ThreadPool pool(4);
    pool.shutdown();
    require(pool.failed_task_count() == 0, "Idle pool reported task failures");
}

int add(int a, int b) { return a + b; }

void futures_and_ownership() {
    threadforge::ThreadPool pool(2);
    auto sum = pool.submit(add, 10, 20);
    auto moved = pool.submit([](std::unique_ptr<int> p) { return *p; }, std::make_unique<int>(7));
    auto capture = pool.submit([p = std::make_unique<int>(9)] { return *p; });
    int borrowed = 0;
    auto reference = pool.submit([](int& value) { value = 5; }, std::ref(borrowed));
    auto failure = pool.submit([]() -> int { throw std::runtime_error("expected"); });
    require(sum.get() == 30 && moved.get() == 7 && capture.get() == 9, "Typed task result mismatch");
    reference.get();
    require(borrowed == 5, "Reference wrapper did not borrow");
    expect_throw<std::runtime_error>([&] { failure.get(); });
    pool.shutdown();
    require(pool.failed_task_count() == 1, "Future failure not counted");
}

void priorities_and_capacity() {
    Gate gate;
    threadforge::ThreadPool pool(1, threadforge::SchedulingPolicy::Priority, 4);
    pool.submit([&] { gate.arrive_and_wait(); });
    gate.wait_for(1);
    std::vector<int> order;
    pool.submit_with_priority(threadforge::Priority::Low, [&] { order.push_back(4); });
    pool.submit_with_priority(threadforge::Priority::High, [&] { order.push_back(1); });
    pool.submit_with_priority(threadforge::Priority::Normal, [&] { order.push_back(3); });
    pool.submit_with_priority(threadforge::Priority::High, [&] { order.push_back(2); });
    bool full = false;
    try { pool.submit([] {}); } catch (const threadforge::QueueFull&) { full = true; }
    gate.open();
    pool.shutdown();
    require(full, "Bounded queue failed to reject overload");
    require(order == std::vector<int>({1, 2, 3, 4}), "Priority or stable ordering failed");
}

void cancellation_and_states() {
    Gate gate;
    std::optional<threadforge::TaskHandle> survivor;
    {
        threadforge::ThreadPool pool(1, threadforge::SchedulingPolicy::Fifo, 1);
        auto running = pool.submit_cancellable([&] { gate.arrive_and_wait(); });
        gate.wait_for(1);
        bool running_rejected = !running.handle.cancel();
        auto queued = pool.submit_cancellable([] { return 42; });
        bool cancelled = queued.handle.cancel();
        bool duplicate_rejected = !queued.handle.cancel();
        // Cancellation frees capacity immediately, even while the worker is blocked.
        auto replacement = pool.submit([] { return 7; });
        survivor = queued.handle;
        gate.open();
        pool.shutdown();
        require(running_rejected && cancelled && duplicate_rejected, "Cancellation transition failed");
        require(queued.handle.state() == threadforge::TaskState::Cancelled, "Cancelled state missing");
        require(running.handle.state() == threadforge::TaskState::Completed, "Completed state missing");
        expect_throw<threadforge::TaskCancelled>([&] { queued.future.get(); });
        require(replacement.get() == 7, "Cancellation did not release capacity");
    }
    require(!survivor->cancel(), "Handle accessed destroyed pool");
}

void cancel_pending_shutdown() {
    Gate gate;
    threadforge::ThreadPool pool(1);
    auto running = pool.submit([&] { gate.arrive_and_wait(); });
    gate.wait_for(1);
    auto queued = pool.submit([] { return 42; });
    std::thread closer([&] { pool.shutdown(threadforge::ShutdownMode::CancelPending); });
    // Pending futures settle before the blocked running task is released.
    bool cancelled = false;
    try { queued.get(); } catch (const threadforge::TaskCancelled&) { cancelled = true; }
    gate.open();
    closer.join();
    running.get();
    require(cancelled, "CancelPending executed queued work");
}

void cancellation_race() {
    threadforge::ThreadPool pool(4);
    std::atomic<unsigned> executed{0};
    std::vector<threadforge::Submission<void>> tasks;
    for (int i = 0; i < 2000; ++i) {
        tasks.push_back(pool.submit_cancellable([&] { ++executed; }));
    }
    unsigned cancelled = 0;
    std::thread canceller([&] {
        for (auto& task : tasks) { if (task.handle.cancel()) { ++cancelled; } }
    });
    pool.shutdown();
    canceller.join();
    unsigned errors = 0;
    for (auto& task : tasks) {
        try { task.future.get(); } catch (const threadforge::TaskCancelled&) { ++errors; }
    }
    require(errors == cancelled && executed.load() + cancelled == tasks.size(), "Cancel/dequeue race lost work");
}

void metrics_accounting() {
    Gate gate;
    threadforge::ThreadPool pool(1);
    auto empty = pool.metrics();
    require(!empty.mean_execution_ns && empty.submitted == 0, "Empty metrics should have no averages");
    pool.submit([&] { gate.arrive_and_wait(); });
    gate.wait_for(1);
    auto cancelled = pool.submit_cancellable([] {});
    pool.submit([] { throw std::runtime_error("failure"); });
    pool.submit([] {});
    const bool did_cancel = cancelled.handle.cancel();
    auto during = pool.metrics();
    gate.open();
    pool.shutdown();
    require(did_cancel && during.queue_depth == 2 && during.running == 1, "Live metrics mismatch");
    auto final = pool.metrics();
    require(final.submitted == 4 && final.completed == 2 && final.failed == 1 && final.cancelled == 1,
            "Terminal metrics mismatch");
    require(final.queue_depth == 0 && final.running == 0 && final.peak_queue_depth == 3,
            "Queue metrics mismatch");
    require(final.timing_samples == 3 && final.mean_queue_wait_ns && final.mean_execution_ns &&
            final.mean_completion_latency_ns && final.throughput_per_second > 0, "Timing metrics missing");
    require(*final.mean_completion_latency_ns >= *final.mean_execution_ns, "Invalid timestamp boundaries");
    threadforge::ThreadPool untimed(1, threadforge::SchedulingPolicy::Fifo, 0, false);
    untimed.submit([] {});
    untimed.shutdown();
    require(!untimed.metrics().mean_execution_ns && untimed.metrics().completed == 1,
            "Disabling timings must preserve counts");
}

void work_stealing() {
    threadforge::ThreadPool pool(2, threadforge::SchedulingPolicy::WorkStealing);
    std::promise<void> submitted, release;
    auto ready = submitted.get_future();
    auto gate = release.get_future();
    std::vector<std::future<int>> children;
    auto parent = pool.submit([&] {
        for (int i = 0; i < 32; ++i) { children.push_back(pool.submit([i] { return i; })); }
        submitted.set_value();
        gate.wait();
    });
    ready.get();
    int sum = 0;
    for (auto& child : children) { sum += child.get(); }
    release.set_value();
    parent.get();
    pool.shutdown();
    require(sum == 496 && pool.metrics().stolen >= 32, "Idle worker did not steal local submissions");
}

void policy_lifecycle_matrix() {
    for (const auto policy : {threadforge::SchedulingPolicy::Fifo, threadforge::SchedulingPolicy::Priority,
                              threadforge::SchedulingPolicy::WorkStealing}) {
        Gate gate;
        threadforge::ThreadPool pool(2, policy, 2);
        pool.submit([&] { gate.arrive_and_wait(); });
        pool.submit([&] { gate.arrive_and_wait(); });
        gate.wait_for(2);
        auto cancelled = pool.submit_cancellable([] { return 42; });
        const bool did_cancel = cancelled.handle.cancel();
        auto shutdown_cancelled = pool.submit([] { return 7; });
        std::thread closer([&] { pool.shutdown(threadforge::ShutdownMode::CancelPending); });
        bool cancellation_error = false;
        try { shutdown_cancelled.get(); } catch (const threadforge::TaskCancelled&) { cancellation_error = true; }
        gate.open();
        closer.join();
        expect_throw<threadforge::TaskCancelled>([&] { cancelled.future.get(); });
        auto counts = pool.metrics();
        require(did_cancel && cancellation_error && counts.completed == 2 && counts.cancelled == 2 &&
                counts.submitted == 4 && counts.queue_depth == 0, "Policy lifecycle mismatch");
    }
}
} // namespace

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"validation", validation}, {"fifo_and_destruction", fifo_and_destruction},
        {"concurrent_workers", concurrent_workers},
        {"producers_exactly_once", producers_exactly_once},
        {"shutdown_race", shutdown_race},
        {"exceptions_and_worker_shutdown", exceptions_and_worker_shutdown},
        {"drain_running_and_queued", drain_running_and_queued},
        {"idle_shutdown", idle_shutdown},
        {"futures_and_ownership", futures_and_ownership},
        {"priorities_and_capacity", priorities_and_capacity},
        {"cancellation_and_states", cancellation_and_states},
        {"cancel_pending_shutdown", cancel_pending_shutdown},
        {"cancellation_race", cancellation_race},
        {"metrics_accounting", metrics_accounting},
        {"work_stealing", work_stealing},
        {"policy_lifecycle_matrix", policy_lifecycle_matrix}
    };
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }
}
