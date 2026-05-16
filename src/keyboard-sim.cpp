#include "keyboard-sim.h"
#include "globals.h"
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "request-storage.h"
#include "screen-manager.h"

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
    if (strcmp(name, "ctrl") == 0) return XK_Control_L;
    if (strcmp(name, "super") == 0) return XK_Super_L;
    if (strcmp(name, "alt") == 0) return XK_Alt_L;
    if (strcmp(name, "space") == 0) return XK_space;
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

static void wait_for_keys_released() {
    while (held_key_count.load(std::memory_order_relaxed) > 0) {
        struct timespec ts = {0, 10000000L};
        nanosleep(&ts, NULL);
    }
}

static Display* open_display_and_focus(const char* text, Window target_window, Window* out_prev_focus) {
    if (debug_enabled) screen_debug("type_text text='%s', target=0x%lx", text, static_cast<unsigned long>(target_window));

    wait_for_keys_released();

    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) {
        if (debug_enabled) screen_debug("type_text failed to open display");
        return nullptr;
    }

    if (debug_enabled) screen_debug("type_text opened display");

    *out_prev_focus = None;
    if (target_window != None) {
        Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom wm_state_hidden = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
        XPropertyEvent pe = {0};
        pe.type = PropertyNotify;
        pe.window = target_window;
        pe.atom = wm_state;
        pe.state = PropertyNewValue;
        XSendEvent(dpy, target_window, False, NoEventMask, (XEvent*)&pe);

        int revert_to = RevertToNone;
        XGetInputFocus(dpy, out_prev_focus, &revert_to);
        XSetInputFocus(dpy, target_window, RevertToParent, CurrentTime);
        XFlush(dpy);
        struct timespec ts = {0, 50000000L};
        nanosleep(&ts, NULL);

        if (debug_enabled) screen_debug("type_text focused window 0x%lx (was 0x%lx)", static_cast<unsigned long>(target_window), static_cast<unsigned long>(*out_prev_focus));
    }

    return dpy;
}

static void type_char(Display* dpy, char c) {
    if (c >= 'a' && c <= 'z') {
        char buf[2] = {c, 0};
        simulate_key(dpy, buf, false);
    } else if (c >= 'A' && c <= 'Z') {
        char buf[2] = {static_cast<char>(c + 32), 0};
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

static void type_characters(Display* dpy, const char* text, std::atomic<bool>& abort_requested, int request_id) {
    for (const char* p = text; *p; p++) {
        if (abort_requested.load()) {
            if (debug_enabled) screen_debug("type_text aborted during character loop");
            break;
        }
        if (request_id > 0 && g_request_storage.is_cancelled(request_id)) {
            if (debug_enabled) screen_debug("type_text request %d cancelled", request_id);
            break;
        }
        type_char(dpy, *p);
    }
}

static void type_special_keys(Display* dpy, const std::vector<std::string>& special_keys, std::atomic<bool>& abort_requested, int request_id) {
    if (debug_enabled) screen_debug("type_text special_keys count=%zu", special_keys.size());

    for (const auto& key : special_keys) {
        if (abort_requested.load()) break;
        if (request_id > 0 && g_request_storage.is_cancelled(request_id)) break;

        if (key == "enter" || key == "Return") {
            simulate_key(dpy, "Return", false);
        } else if (key == "space" || key == " ") {
            simulate_key(dpy, "space", false);
        } else if (key == "tab" || key == "Tab") {
            simulate_key(dpy, "Tab", false);
        } else {
            simulate_key(dpy, key.c_str(), false);
        }
    }
}

static void cleanup_type_text(Display* dpy, Window target_window, Window prev_focus) {
    if (debug_enabled) screen_debug("type_text flushing");
    XFlush(dpy);
    struct timespec ts = {0, 10000000L};
    nanosleep(&ts, NULL);

    if (target_window != None && prev_focus != None) {
        XSetInputFocus(dpy, prev_focus, RevertToParent, CurrentTime);
        XFlush(dpy);
        if (debug_enabled) screen_debug("type_text restored focus to 0x%lx", static_cast<unsigned long>(prev_focus));
    }

    if (debug_enabled) screen_debug("type_text closing display");
    XCloseDisplay(dpy);
}

void type_text(const char* text, const std::vector<std::string>& special_keys, Window target_window, std::atomic<bool>& abort_requested, int request_id) {
    if (!text) return;

    Window prev_focus = None;
    Display* dpy = open_display_and_focus(text, target_window, &prev_focus);
    if (!dpy) return;

    type_characters(dpy, text, abort_requested, request_id);

    if (abort_requested.load()) {
        if (debug_enabled) screen_debug("type_text aborted before special keys");
    } else if (request_id > 0 && g_request_storage.is_cancelled(request_id)) {
        if (debug_enabled) screen_debug("type_text request %d cancelled before special keys", request_id);
    } else {
        type_special_keys(dpy, special_keys, abort_requested, request_id);
    }

    cleanup_type_text(dpy, target_window, prev_focus);
}
