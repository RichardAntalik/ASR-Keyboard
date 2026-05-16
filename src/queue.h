#ifndef QUEUE_H
#define QUEUE_H

#include <vector>
#include <mutex>
#include <condition_variable>
#include "keyboard-sim.h"

struct queue_item {
    std::vector<short> buffer;
    std::vector<std::string> special_keys;
    std::string prompt;
    Window target_window;
    int request_id;
};

class thread_safe_queue {
public:
    void push(const queue_item& item);
    queue_item pop();
    bool empty();
    size_t size();
    void abort_and_drain();

private:
    std::vector<queue_item> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

#endif
