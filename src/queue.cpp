#include "queue.h"

extern std::atomic<bool> abort_requested;

void thread_safe_queue::push(const queue_item& item) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(item);
    }
    cv_.notify_one();
}

queue_item thread_safe_queue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || abort_requested.load(); });
    if (queue_.empty() || abort_requested.load()) {
        return queue_item{nullptr, 0, nullptr, 0, nullptr, None};
    }
    queue_item item = queue_.front();
    queue_.erase(queue_.begin());
    return item;
}

bool thread_safe_queue::empty() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

size_t thread_safe_queue::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void thread_safe_queue::abort_and_drain() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    cv_.notify_all();
}
