#include "vu-thread.h"
#include "screen-manager.h"
#include "globals.h"
#include <curses.h>
#include <thread>
#include <chrono>

static std::atomic<bool> vu_running{false};
static std::thread vu_thread;

static void vu_thread_func() {
    while (vu_running.load()) {
        {
            std::lock_guard<std::mutex> lock(screen_mutex);
            screen_draw_vu_meter(current_volume.load());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void vu_thread_start() {
    vu_running.store(true);
    vu_thread = std::thread(vu_thread_func);
}

void vu_thread_stop() {
    vu_running.store(false);
}

void vu_thread_wait() {
    if (vu_thread.joinable()) {
        vu_thread.join();
    }
}
