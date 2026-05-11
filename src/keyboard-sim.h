#ifndef KEYBOARD_SIM_H
#define KEYBOARD_SIM_H

#include <X11/Xlib.h>

#define MAX_PROMPT 512
#define MAX_SHORTCUT_KEYS 8
#define MAX_SOURCES 64

struct shortcut_entry {
    char keys[MAX_SHORTCUT_KEYS][64];
    int key_count;
    char prompt[MAX_PROMPT];
    char special_key[64];
};

struct config {
    shortcut_entry entries[MAX_SOURCES];
    int entry_count;
};

KeySym config_key_to_keysym(const char* name);
void simulate_key(Display* dpy, const char* keysym_name, bool shift);
void type_text(const char* text, const char* special_key);

#endif
