#include "threadforge/thread_pool.hpp"
#include "workloads.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
struct Config { std::size_t workers; threadforge::SchedulingPolicy policy; bool timing; };
struct Result {
    double elapsed = 0;
    double mean = 0;
    double p50 = 0;
    double p95 = 0;
    double p99 = 0;
    std::uint64_t checksum = 0;
    threadforge::Metrics metrics;
};
double percentile(const std::vector<double>& sorted, double percentile_value) {
    const auto index = static_cast<std::size_t>(std::ceil(percentile_value * static_cast<double>(sorted.size()))) - 1;
    return sorted[index];
}
Result measure(const std::string& workload, std::size_t count, std::size_t iterations, Config config) {
    std::vector<double> latency(count);
    std::vector<std::uint64_t> values(count);
    std::vector<std::future<std::uint64_t>> futures;
    futures.reserve(count);
    std::unique_ptr<threadforge::ThreadPool> pool;
    if (config.workers != 0) {
        pool = std::make_unique<threadforge::ThreadPool>(config.workers, config.policy, 0, config.timing);
    }
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        const auto admitted = Clock::now();
        auto work = [&, i, admitted] {
            const auto value = run_workload(workload, i, iterations);
            latency[i] = std::chrono::duration<double, std::nano>(Clock::now() - admitted).count();
            return value;
        };
        if (pool) { futures.push_back(pool->submit(std::move(work))); }
        else { values[i] = work(); }
    }
    if (pool) {
        for (std::size_t i = 0; i < count; ++i) { values[i] = futures[i].get(); }
        pool->shutdown();
    }
    Result result;
    result.elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    if (pool) { result.metrics = pool->metrics(); }
    else {
        result.metrics.mean_queue_wait_ns = 0;
        result.metrics.mean_execution_ns = std::accumulate(latency.begin(), latency.end(), 0.0) / static_cast<double>(count);
    }
    result.checksum = std::accumulate(values.begin(), values.end(), std::uint64_t{0});
    result.mean = std::accumulate(latency.begin(), latency.end(), 0.0) / static_cast<double>(count);
    std::sort(latency.begin(), latency.end());
    result.p50 = percentile(latency, 0.50);
    result.p95 = percentile(latency, 0.95);
    result.p99 = percentile(latency, 0.99);
    return result;
}
std::size_t number(const std::string& text, std::size_t maximum) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (text.empty() || text.front() == '-' || consumed != text.size() || value == 0 || value > maximum) {
        throw std::invalid_argument("Option outside supported range");
    }
    return static_cast<std::size_t>(value);
}
void optional(std::ostream& out, std::optional<double> value) { if (value) { out << *value; } }
}

int main(int argc, char** argv) {
    try {
        std::size_t tasks = 2000, repeats = 3, iterations = 20000;
        std::size_t max_workers = std::max(8U, std::min(16U, std::thread::hardware_concurrency()));
        std::string output;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "threadforge_benchmark [--tasks N] [--repeats N] [--iterations N] [--max-workers N] [--output file.csv]\n";
                return 0;
            }
            if (++i == argc) { throw std::invalid_argument("Missing option value"); }
            if (option == "--tasks") { tasks = number(argv[i], 1000000); }
            else if (option == "--repeats") { repeats = number(argv[i], 100); }
            else if (option == "--iterations") { iterations = number(argv[i], 1000000); }
            else if (option == "--max-workers") { max_workers = number(argv[i], 256); }
            else if (option == "--output") { output = argv[i]; }
            else { throw std::invalid_argument("Unknown option: " + option); }
        }
        std::ofstream file;
        if (!output.empty()) {
            file.open(output);
            if (!file) { throw std::runtime_error("Cannot open benchmark output"); }
        }
        std::ostream& out = output.empty() ? std::cout : file;
        out << std::setprecision(12);
        out << "workload,tasks,iterations,workers,policy,timing,repeat,elapsed_s,tasks_per_s,latency_mean_ns,p50_ns,p95_ns,p99_ns,mean_queue_ns,mean_execution_ns,scaling_efficiency,checksum\n";
        std::vector<Config> configs{{0, threadforge::SchedulingPolicy::Fifo, false}};
        std::vector<std::size_t> workers;
        for (std::size_t n = 1; n <= max_workers; n *= 2) { workers.push_back(n); }
        if (workers.back() != max_workers) { workers.push_back(max_workers); }
        for (const auto n : workers) {
            configs.push_back({n, threadforge::SchedulingPolicy::Fifo, true});
            configs.push_back({n, threadforge::SchedulingPolicy::Fifo, false});
            configs.push_back({n, threadforge::SchedulingPolicy::Priority, true});
        }
        for (const std::string workload : {"cpu", "short", "mixed", "blocking", "batch"}) {
            const auto count = workload == "batch" ? tasks * 10 : tasks;
            for (const auto config : configs) { measure(workload, std::min<std::size_t>(count, 100), iterations, config); }
            for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
                const Result serial = measure(workload, count, iterations, configs.front());
                // Alternate configuration order to reduce fixed-order drift bias.
                auto order = configs;
                if (repeat % 2 != 0) { std::reverse(order.begin() + 1, order.end()); }
                for (const auto config : order) {
                    const auto result = config.workers == 0 ? serial : measure(workload, count, iterations, config);
                    if (result.checksum != serial.checksum) { throw std::runtime_error("Benchmark checksum mismatch"); }
                    out << workload << ',' << count << ',' << iterations << ',' << config.workers << ','
                        << (config.workers == 0 ? "serial" : config.policy == threadforge::SchedulingPolicy::Fifo ? "fifo" : "priority")
                        << ',' << config.timing << ',' << repeat << ',' << result.elapsed << ','
                        << static_cast<double>(count) / result.elapsed << ',' << result.mean << ','
                        << result.p50 << ',' << result.p95 << ',' << result.p99 << ',';
                    optional(out, result.metrics.mean_queue_wait_ns);
                    out << ',';
                    optional(out, result.metrics.mean_execution_ns);
                    out << ',' << serial.elapsed / result.elapsed / static_cast<double>(std::max<std::size_t>(1, config.workers))
                        << ',' << result.checksum << '\n';
                }
            }
        }
        out.flush();
        if (!out) { throw std::runtime_error("Failed writing benchmark results"); }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
