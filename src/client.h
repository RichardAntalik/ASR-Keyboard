#ifndef SERVER_H
#define SERVER_H

#include <X11/Xlib.h>
#include <pulse/pulseaudio.h>
#include <atomic>
#include <vector>
#include <string>

inline constexpr char const* SERVER_URL = "http://127.0.0.1:8000/transcribe";

void send_to_server(short* buffer, size_t size, const std::vector<std::string>& special_keys,
                     const char* prompt, Window target_window, std::atomic<bool>& abort_requested, int request_id);

#endif
