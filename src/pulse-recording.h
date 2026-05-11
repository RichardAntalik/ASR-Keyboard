#ifndef PULSE_RECORDING_H
#define PULSE_RECORDING_H

#include <pulse/pulseaudio.h>
#include <atomic>

#define MAX_AUDIO_SIZE (1024 * 8192)
#define MAX_SOURCE_NAME 256

extern std::atomic<bool> recording_active;
extern char source_name[MAX_SOURCE_NAME];

struct record_state {
    short* buffer;
    size_t total;
};

void run_pa_query(int index);
void* record_thread(void* arg);

#endif
