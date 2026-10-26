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
    expect_throw<std::invalid_argument>([&] { pool.submit({}); });
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
} // namespace

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"validation", validation}, {"fifo_and_destruction", fifo_and_destruction},
        {"concurrent_workers", concurrent_workers},
        {"producers_exactly_once", producers_exactly_once},
        {"shutdown_race", shutdown_race},
        {"exceptions_and_worker_shutdown", exceptions_and_worker_shutdown},
        {"drain_running_and_queued", drain_running_and_queued},
        {"idle_shutdown", idle_shutdown}
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
