#include "threadforge/thread_pool.hpp"
#include <iostream>
#include <memory>
#include <vector>
int main() {
    threadforge::ThreadPool pool(4);
    std::vector<std::future<int>> results;
    for (int i = 0; i < 10; ++i) { results.push_back(pool.submit([](int n) { return n * n; }, i)); }
    auto owned = pool.submit([](std::unique_ptr<int> p) { return *p; }, std::make_unique<int>(42));
    auto failure = pool.submit([]() -> int { throw std::runtime_error("Example task failed"); });
    int sum = 0;
    for (auto& result : results) { sum += result.get(); }
    std::cout << "Sum of squares: " << sum << "; moved value: " << owned.get() << '\n';
    try { failure.get(); } catch (const std::exception& error) { std::cout << "Future exception: " << error.what() << '\n'; }
    pool.shutdown();
    const auto metrics = pool.metrics();
    std::cout << "Completed: " << metrics.completed << "; failed: " << metrics.failed << '\n';
    return sum == 285 && metrics.completed == 11 && metrics.failed == 1 ? 0 : 1;
}
