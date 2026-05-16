#ifndef GLOBALS_H
#define GLOBALS_H

#include <atomic>
#include <string>

extern std::atomic<bool> recording_active;
extern std::atomic<bool> abort_requested;
extern std::atomic<int> held_key_count;
extern bool debug_enabled;
extern std::string config_path_str;
extern std::atomic<float> current_volume;

#endif
