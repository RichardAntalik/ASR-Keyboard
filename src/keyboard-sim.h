#ifndef KEYBOARD_SIM_H
#define KEYBOARD_SIM_H

#include <X11/Xlib.h>
#include <atomic>

extern bool debug_enabled;

struct shortcut_entry {
    char** keys;
    int key_count;
    char* prompt;
    char** special_keys;
    int special_key_count;
};

struct config {
    shortcut_entry* entries;
    int entry_count;
};

KeySym config_key_to_keysym(const char* name);
void simulate_key(Display* dpy, const char* keysym_name, bool shift);
Window get_active_window(Display* dpy);
void type_text(const char* text, const char* const* special_keys, int special_key_count, Window target_window, std::atomic<bool>& abort_requested, int request_id = 0);

#endif
