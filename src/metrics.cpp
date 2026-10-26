#include "threadforge/metrics.hpp"
namespace threadforge::detail {
Metrics MetricsData::snapshot(std::size_t queue_depth, bool stopped_state) const {
    Metrics result = counts;
    result.queue_depth = queue_depth;
    if (counts.timing_samples != 0) {
        const auto samples = static_cast<double>(counts.timing_samples);
        result.mean_queue_wait_ns = queue_wait_ns / samples;
        result.mean_execution_ns = execution_ns / samples;
        result.mean_completion_latency_ns = completion_ns / samples;
    }
    const double elapsed = std::chrono::duration<double>(
        (stopped_state ? stopped : Clock::now()) - created).count();
    if (elapsed > 0) {
        result.throughput_per_second = static_cast<double>(counts.completed + counts.failed) / elapsed;
    }
    return result;
}
}
