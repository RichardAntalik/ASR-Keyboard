#ifndef REQUEST_STORAGE_H
#define REQUEST_STORAGE_H

#include <unordered_map>
#include <mutex>
#include <atomic>

class request_storage {
public:
    int add_request();
    void cancel_all();
    bool is_cancelled(int request_id);
    void mark_cancelled(int request_id);
    void remove(int request_id);
    size_t pending_count();

private:
    struct request_entry {
        bool cancelled = false;
    };
    std::unordered_map<int, request_entry> requests_;
    std::mutex mutex_;
    std::atomic<int> next_id_{0};
};

extern request_storage g_request_storage;

#endif
