#include "threadforge/thread_pool.hpp"
#include <atomic>
#include <iostream>
#include <vector>

int main() {
    constexpr std::size_t count = 30000;
    for (const auto policy : {threadforge::SchedulingPolicy::Fifo, threadforge::SchedulingPolicy::Priority,
                              threadforge::SchedulingPolicy::WorkStealing}) {
        std::vector<std::atomic<unsigned>> hits(count);
        for (auto& hit : hits) { hit.store(0); }
        std::atomic<unsigned> cancelled{0}, failed{0}, completed{0};
        std::atomic<bool> bad_result{false};
        threadforge::ThreadPool pool(4, policy);
        std::vector<std::thread> producers;
        for (std::size_t producer = 0; producer < 4; ++producer) {
            producers.emplace_back([&, producer] {
                std::vector<threadforge::Submission<std::size_t>> tasks;
                for (std::size_t i = producer; i < count; i += 4) {
                    auto task = pool.submit_cancellable_with_priority(static_cast<threadforge::Priority>(i % 3), [&, i] {
                        hits[i].fetch_add(1);
                        if (i % 17 == 0) { throw std::runtime_error("stress failure"); }
                        return i;
                    });
                    if (i % 3 == 0) { task.handle.cancel(); }
                    tasks.push_back(std::move(task));
                }
                std::size_t expected = producer;
                for (auto& task : tasks) {
                    try {
                        if (task.future.get() != expected) { bad_result.store(true); }
                        ++completed;
                    } catch (const threadforge::TaskCancelled&) { ++cancelled; }
                    catch (const std::runtime_error&) { ++failed; }
                    expected += 4;
                }
            });
        }
        for (auto& producer : producers) { producer.join(); }
        pool.shutdown();
        std::size_t invoked = 0;
        for (const auto& hit : hits) {
            if (hit.load() > 1) { std::cerr << "Duplicate execution\n"; return 1; }
            invoked += hit.load();
        }
        const auto metrics = pool.metrics();
        if (bad_result.load() || invoked + cancelled.load() != count || metrics.submitted != count ||
            metrics.completed != completed.load() || metrics.failed != failed.load() ||
            metrics.cancelled != cancelled.load() || metrics.queue_depth != 0 || metrics.running != 0) {
            std::cerr << "Stress accounting failed\n";
            return 1;
        }
        std::cout << "PASS policy " << static_cast<int>(policy) << ": " << count
                  << " tasks, " << metrics.completed << " completed, " << metrics.failed
                  << " failed, " << metrics.cancelled << " cancelled\n";
    }
}
