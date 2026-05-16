#include "vu-thread.h"
#include "screen-manager.h"
#include <curses.h>
#include <pthread.h>
#include <unistd.h>
#include <atomic>

extern float current_volume;

static std::atomic<bool> vu_running{false};
static pthread_t vu_thread_id;
pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* vu_thread_func(void*) {
    while (vu_running.load()) {
        pthread_mutex_lock(&screen_mutex);
        screen_draw_vu_meter(current_volume);
        pthread_mutex_unlock(&screen_mutex);
        usleep(50000);
    }
    return nullptr;
}

void vu_thread_start() {
    vu_running.store(true);
    pthread_create(&vu_thread_id, nullptr, vu_thread_func, nullptr);
}

void vu_thread_stop() {
    vu_running.store(false);
}

void vu_thread_wait() {
    if (vu_running.load()) {
        pthread_join(vu_thread_id, nullptr);
    }
}
