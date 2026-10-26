#include "workloads.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

std::uint64_t run_workload(const std::string& name, std::size_t task, std::size_t iterations) {
    std::size_t rounds = iterations;
    if (name == "short") { rounds = 8; }
    else if (name == "batch") { rounds = 16; }
    else if (name == "mixed") { rounds = task % 10 == 0 ? iterations * 10 : iterations / 10 + 1; }
    else if (name == "blocking") {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        rounds = 8;
    } else if (name != "cpu") { throw std::invalid_argument("Unknown workload"); }
    std::uint64_t value = static_cast<std::uint64_t>(task) + 0x9e3779b97f4a7c15ULL;
    for (std::size_t i = 0; i < rounds; ++i) {
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        value *= 2685821657736338717ULL;
    }
    return value;
}
