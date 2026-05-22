#ifndef GLOBALS_H
#define GLOBALS_H

#include <atomic>
#include <string>
#include <cstdio>
#include <thread>
#include <cstddef>

extern std::atomic<bool> recording_active;
extern std::atomic<bool> abort_requested;
extern std::atomic<int> held_key_count;
extern bool debug_enabled;
extern std::string config_path_str;
extern std::atomic<float> current_volume;

extern const char* g_server_url;
extern FILE* g_server_stdout;
extern std::thread g_server_reader_thread;
extern pid_t g_server_pid;

#endif
