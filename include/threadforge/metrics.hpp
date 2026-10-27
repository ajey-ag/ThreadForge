#pragma once
#include <chrono>
#include <cstddef>
#include <optional>

namespace threadforge {
struct Metrics {
    std::size_t submitted = 0;
    std::size_t completed = 0; // Successful tasks only.
    std::size_t failed = 0;
    std::size_t cancelled = 0;
    std::size_t running = 0;
    std::size_t queue_depth = 0;
    std::size_t peak_queue_depth = 0;
    std::size_t timing_samples = 0; // Completed + failed, when timing is enabled.
    std::size_t stolen = 0; // Work actually dequeued from another worker's deque.
    std::optional<double> mean_queue_wait_ns;
    std::optional<double> mean_execution_ns;
    std::optional<double> mean_completion_latency_ns;
    double throughput_per_second = 0; // (Completed + failed) / pool lifetime.
};
namespace detail {
using Clock = std::chrono::steady_clock;
struct MetricsData {
    Metrics counts;
    double queue_wait_ns = 0;
    double execution_ns = 0;
    double completion_ns = 0;
    Clock::time_point created = Clock::now();
    Clock::time_point stopped{};
    Metrics snapshot(std::size_t queue_depth, bool stopped_state) const;
};
}
}
