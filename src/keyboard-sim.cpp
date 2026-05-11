#include "keyboard-sim.h"
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <string.h>
#include <stdio.h>

extern bool debug_enabled;

KeySym config_key_to_keysym(const char* name) {
    if (strcmp(name, "ctrl") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(ctrl)=%ld\n", (long)XK_Control_L); return XK_Control_L; }
    if (strcmp(name, "super") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(super)=%ld\n", (long)XK_Super_L); return XK_Super_L; }
    if (strcmp(name, "alt") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(alt)=%ld\n", (long)XK_Alt_L); return XK_Alt_L; }
    if (strcmp(name, "space") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(space)=%ld\n", (long)XK_space); return XK_space; }
    return XStringToKeysym(name);
}

void simulate_key(Display* dpy, const char* keysym_name, bool shift) {
    KeyCode code = XKeysymToKeycode(dpy, XStringToKeysym(keysym_name));
    if (code == 0) return;

    if (shift) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, 0);
    XTestFakeKeyEvent(dpy, code, True, 0);
    XTestFakeKeyEvent(dpy, code, False, 0);
    if (shift) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, 0);
}

void type_text(const char* text, const char* const* special_keys, int special_key_count) {
    if (debug_enabled) printf("Debug: type_text text=%s, special_key_count=%d\n", text, special_key_count);
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    if (debug_enabled) printf("Debug: type_text opened display\n");

    for (const char* p = text; *p; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            char buf[2] = {c, 0};
            simulate_key(dpy, buf, false);
        } else if (c >= 'A' && c <= 'Z') {
            char buf[2] = {(char)(c + 32), 0};
            simulate_key(dpy, buf, true);
        } else if (c >= '0' && c <= '9') {
            char buf[2] = {c, 0};
            simulate_key(dpy, buf, false);
        } else {
            switch (c) {
                case ' ':  simulate_key(dpy, "space", false); break;
                case '.':  simulate_key(dpy, "period", false); break;
                case ',':  simulate_key(dpy, "comma", false); break;
                case '?':  simulate_key(dpy, "slash", true); break;
                case '!':  simulate_key(dpy, "1", true); break;
                case '-':  simulate_key(dpy, "minus", false); break;
                case '_':  simulate_key(dpy, "minus", true); break;
                case '\'': simulate_key(dpy, "apostrophe", false); break;
                case '"':  simulate_key(dpy, "apostrophe", true); break;
                case ':':  simulate_key(dpy, "semicolon", true); break;
                case ';':  simulate_key(dpy, "semicolon", false); break;
                case '+':  simulate_key(dpy, "equal", true); break;
                case '=':  simulate_key(dpy, "equal", false); break;
                case '/':  simulate_key(dpy, "slash", false); break;
                case '(':  simulate_key(dpy, "9", true); break;
                case ')':  simulate_key(dpy, "0", true); break;
                default:   break;
            }
        }
    }

    if (debug_enabled) printf("Debug: type_text special_keys count=%d\n", special_key_count);
    for (int i = 0; i < special_key_count; i++) {
        if (strcmp(special_keys[i], "enter") == 0 || strcmp(special_keys[i], "Return") == 0) {
            simulate_key(dpy, "Return", false);
        } else if (strcmp(special_keys[i], "space") == 0 || strcmp(special_keys[i], " ") == 0) {
            simulate_key(dpy, "space", false);
        } else if (strcmp(special_keys[i], "tab") == 0 || strcmp(special_keys[i], "Tab") == 0) {
            simulate_key(dpy, "Tab", false);
        } else {
            simulate_key(dpy, special_keys[i], false);
        }
    }

    if (debug_enabled) printf("Debug: type_text flushing\n");
    XFlush(dpy);
    if (debug_enabled) printf("Debug: type_text closing display\n");
    XCloseDisplay(dpy);
}
