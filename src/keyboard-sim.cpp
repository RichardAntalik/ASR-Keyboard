#include "keyboard-sim.h"
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

extern bool debug_enabled;

Window get_active_window(Display* dpy) {
    Atom net_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    if (net_active == None) return None;

    Window active_window = None;
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char* prop = NULL;

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_active, 0, 1, False,
                           XA_WINDOW, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success &&
        prop != NULL && nitems > 0) {
        active_window = *(Window*)prop;
        XFree(prop);
    }

    return active_window;
}

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

void type_text(const char* text, const char* const* special_keys, int special_key_count, Window target_window, std::atomic<bool>& abort_requested) {
    if (debug_enabled) printf("Debug: type_text text=%s, special_key_count=%d, target=0x%lx\n", text, special_key_count, (unsigned long)target_window);
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    if (debug_enabled) printf("Debug: type_text opened display\n");

    Window prev_focus = None;
    int revert_to = RevertToNone;

    if (target_window != None) {
        Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom wm_state_hidden = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
        XPropertyEvent pe = {0};
        pe.type = PropertyNotify;
        pe.window = target_window;
        pe.atom = wm_state;
        pe.state = PropertyNewValue;
        XSendEvent(dpy, target_window, False, NoEventMask, (XEvent*)&pe);

        XGetInputFocus(dpy, &prev_focus, &revert_to);
        XSetInputFocus(dpy, target_window, RevertToParent, CurrentTime);
        XFlush(dpy);
        struct timespec ts = {0, 50000000L};
        nanosleep(&ts, NULL);

        if (debug_enabled) printf("Debug: type_text focused window 0x%lx (was 0x%lx)\n", (unsigned long)target_window, (unsigned long)prev_focus);
    }

    for (const char* p = text; *p; p++) {
        if (abort_requested.load()) {
            if (debug_enabled) printf("Debug: type_text aborted during character loop\n");
            break;
        }
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

    if (abort_requested.load()) {
         if (debug_enabled) printf("Debug: type_text aborted before special keys\n");
         goto cleanup;
    }

    if (debug_enabled) printf("Debug: type_text special_keys count=%d\n", special_key_count);
    for (int i = 0; i < special_key_count; i++) {
        if (abort_requested.load()) break;
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

cleanup:
    if (debug_enabled) printf("Debug: type_text flushing\n");
    XFlush(dpy);
    struct timespec ts2 = {0, 10000000L};
    nanosleep(&ts2, NULL);

    if (target_window != None && prev_focus != None) {
        XSetInputFocus(dpy, prev_focus, RevertToParent, CurrentTime);
        XFlush(dpy);
        if (debug_enabled) printf("Debug: type_text restored focus to 0x%lx\n", (unsigned long)prev_focus);
    }

    if (debug_enabled) printf("Debug: type_text closing display\n");
    XCloseDisplay(dpy);
}
