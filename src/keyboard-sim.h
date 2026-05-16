#ifndef KEYBOARD_SIM_H
#define KEYBOARD_SIM_H

#include <X11/Xlib.h>
#include <atomic>
#include <vector>
#include <string>

extern bool debug_enabled;

struct shortcut_entry {
    std::vector<std::string> keys;
    std::string prompt;
    std::vector<std::string> special_keys;
};

struct config {
    std::vector<shortcut_entry> entries;
};

KeySym config_key_to_keysym(const char* name);
void simulate_key(Display* dpy, const char* keysym_name, bool shift);
Window get_active_window(Display* dpy);
void type_text(const char* text, const std::vector<std::string>& special_keys, Window target_window, std::atomic<bool>& abort_requested, int request_id = 0);

#endif
