#pragma once
#include <stdexcept>
namespace threadforge {
class PoolClosed : public std::runtime_error {
public: PoolClosed() : std::runtime_error("ThreadPool is shutting down") {}
};
class QueueFull : public std::runtime_error {
public: QueueFull() : std::runtime_error("ThreadPool queue capacity reached") {}
};
class TaskCancelled : public std::runtime_error {
public: TaskCancelled() : std::runtime_error("Task cancelled before execution") {}
};
}
