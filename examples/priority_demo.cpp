#include "threadforge/thread_pool.hpp"
#include <iostream>
#include <vector>
int main() {
    threadforge::ThreadPool pool(1, threadforge::SchedulingPolicy::Priority, 4);
    std::promise<void> started, release;
    auto gate = release.get_future();
    pool.submit([&] { started.set_value(); gate.wait(); });
    started.get_future().wait();
    std::vector<int> order;
    auto low = pool.submit_with_priority(threadforge::Priority::Low, [&] { order.push_back(3); });
    auto high = pool.submit_with_priority(threadforge::Priority::High, [&] { order.push_back(1); });
    auto normal = pool.submit([&] { order.push_back(2); });
    auto cancelled = pool.submit_cancellable([] { return 99; });
    const bool did_cancel = cancelled.handle.cancel();
    release.set_value();
    pool.shutdown();
    low.get(); high.get(); normal.get();
    try { cancelled.future.get(); } catch (const threadforge::TaskCancelled& error) { std::cout << error.what() << '\n'; }
    std::cout << "Priority order:";
    for (int value : order) { std::cout << ' ' << value; }
    std::cout << '\n';
    return did_cancel && order == std::vector<int>({1, 2, 3}) ? 0 : 1;
}
