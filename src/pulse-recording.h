#ifndef PULSE_RECORDING_H
#define PULSE_RECORDING_H

#include <pulse/pulseaudio.h>
#include <atomic>
#include <vector>
#include <future>

inline constexpr size_t MAX_AUDIO_SIZE = 1024 * 8192;
inline constexpr size_t MAX_SOURCE_NAME = 256;

extern std::string source_name;

struct record_state {
    std::vector<short> buffer;
    size_t total = 0;
};

void run_pa_query(int index);
void record_thread(std::promise<record_state*>* prom);

#endif
