#include "threadforge/thread_pool.hpp"

#include <array>
#include <atomic>
#include <iostream>

int main() {
    std::array<std::atomic<unsigned>, 100> executions{};
    for (auto& count : executions) { count.store(0); }
    threadforge::ThreadPool pool(4);
    for (std::size_t i = 0; i < executions.size(); ++i) {
        pool.submit([&, i] { executions[i].fetch_add(1); });
    }
    pool.shutdown();
    for (const auto& count : executions) {
        if (count.load() != 1) {
            std::cerr << "Exactly-once verification failed\n";
            return 1;
        }
    }
    std::cout << "Verified: 100 tasks executed exactly once.\n";
}
