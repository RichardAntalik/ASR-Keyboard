#include "request-storage.h"
#include <cstdio>

extern bool debug_enabled;

request_storage g_request_storage;

int request_storage::add_request() {
    int id = ++next_id_;
    std::lock_guard<std::mutex> lock(mutex_);
    requests_[id] = {};
    if (debug_enabled) { printf("Debug: request_storage::add_request -> id=%d, total=%zu\n", id, requests_.size()); fflush(stdout); }
    return id;
}

void request_storage::cancel_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (auto& [id, entry] : requests_) {
        entry.cancelled = true;
        count++;
    }
    if (debug_enabled) { printf("Debug: request_storage::cancel_all -> marked %d requests as cancelled (total in map=%zu)\n", count, requests_.size()); fflush(stdout); }
}

bool request_storage::is_cancelled(int request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(request_id);
    if (it == requests_.end()) return false;
    return it->second.cancelled;
}

void request_storage::mark_cancelled(int request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(request_id);
    if (it != requests_.end()) {
        it->second.cancelled = true;
    }
}

void request_storage::remove(int request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.erase(request_id);
    if (debug_enabled) { printf("Debug: request_storage::remove id=%d, remaining=%zu\n", request_id, requests_.size()); fflush(stdout); }
}

size_t request_storage::pending_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
}
