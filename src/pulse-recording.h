#ifndef PULSE_RECORDING_H
#define PULSE_RECORDING_H

#include <pulse/pulseaudio.h>
#include <atomic>

#define MAX_AUDIO_SIZE (1024 * 8192)
#define MAX_SOURCE_NAME 256

extern std::atomic<bool> recording_active;
extern char source_name[MAX_SOURCE_NAME];
extern float current_volume;

struct record_state {
    short* buffer;
    size_t total;
    pa_volume_t* last_volume;
    bool volume_valid;
};

void run_pa_query(int index);
void* record_thread(void* arg);

#endif
