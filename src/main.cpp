#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>
#include <csignal>
#include <thread>
#include <mutex>
#include <future>
#include <memory>
#include <vector>
#include <string>

#include "json.hpp"
#include "config-parsing.h"
#include "pulse-recording.h"
#include "keyboard-sim.h"
#include "client.h"
#include "queue.h"
#include "screen-manager.h"
#include "request-storage.h"
#include "vu-thread.h"
#include "globals.h"

std::atomic<bool> recording_active{false};
std::atomic<bool> abort_requested{false};
std::atomic<int> held_key_count{0};
bool debug_enabled{false};
std::string config_path_str;

thread_safe_queue transcription_queue;

static config* g_cfg_for_resize = nullptr;
static volatile sig_atomic_t resize_pending = false;

extern std::mutex screen_mutex;
extern void screen_handle_resize(const config* cfg);

static void sigwinch_handler(int) {
    resize_pending = 1;
}

static void parse_args(int argc, char* argv[], bool* remember_window) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            run_pa_query(-1);
            exit(0);
        }
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            run_pa_query(atoi(argv[i+1]));
            i++;
        }
        if (strcmp(argv[i], "-d") == 0) {
            debug_enabled = true;
        }
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path_str = argv[i+1];
            i++;
        }
        if (strcmp(argv[i], "-a") == 0) {
            *remember_window = true;
        }
        if (strcmp(argv[i], "-h") == 0) {
            printf("%s — hold a hotkey to record audio, release to transcribe speech and type the result into the window that was active when you pressed the hotkey.\n", argv[0]);
            printf("Usage: %s [-l] [-i <index>] [-d] [-c <config.json>] [-a] [-h]\n", argv[0]);
            printf("  -l    list audio sources\n");
            printf("  -i <index> select audio source by index\n");
            printf("  -d    enable debug output\n");
            printf("  -c <config.json> specify config file\n");
            printf("  -a    type into the window that has focus when the server responds\n");
            printf("  -h    show help\n");
            exit(0);
        }
    }
}

static const char* resolve_config_path() {
    if (!config_path_str.empty()) return config_path_str.c_str();

    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    const char* home_dir = getenv("HOME");

    if (xdg_config) {
        config_path_str = std::string(xdg_config) + "/asr-kb/config.json";
    } else if (home_dir) {
        config_path_str = std::string(home_dir) + "/.config/asr-kb/config.json";
    } else {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd))) exit(1);
        config_path_str = std::string(cwd) + "/.config/asr-kb/config.json";
    }
    return config_path_str.c_str();
}

static int init_x11(Display** out_dpy) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) exit(1);

    int xi_opcode, event, error;
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) exit(1);

    XIEventMask evmask;
    unsigned char mask[XIMaskLen(XI_LASTEVENT)] = { 0 };
    evmask.deviceid = XIAllMasterDevices;
    evmask.mask_len = sizeof(mask);
    evmask.mask = mask;
    XISetMask(mask, XI_RawKeyPress);
    XISetMask(mask, XI_RawKeyRelease);
    XISelectEvents(dpy, DefaultRootWindow(dpy), &evmask, 1);

    *out_dpy = dpy;
    return xi_opcode;
}

static config* load_or_create_config() {
    const char* path = resolve_config_path();
    config* cfg = load_config(path);

    if (cfg == nullptr) {
        char response = getchar();
        if (response == 'y' || response == 'Y') {
            if (create_default_config(path)) {
                cfg = load_config(path);
            } else {
                return nullptr;
            }
        } else {
            return nullptr;
        }
    }

    return cfg;
}

static void init_screen(config* cfg) {
    g_cfg_for_resize = cfg;
    signal(SIGWINCH, sigwinch_handler);

    screen_init();
    screen_draw_shortcuts(cfg);
    screen_draw_vu_meter(0.0f);
}

static void worker_thread_func() {
    while (true) {
        queue_item item = transcription_queue.pop();
        if (item.buffer.empty()) break;

        if (g_request_storage.is_cancelled(item.request_id)) {
            if (debug_enabled) printf("Debug: worker request %d cancelled, skipping\n", item.request_id);
            g_request_storage.remove(item.request_id);
            continue;
        }

        if (debug_enabled) printf("Debug: worker processing request %d, %zu samples\n", item.request_id, item.buffer.size());
        send_to_server(item.buffer.data(), item.buffer.size(), item.special_keys, item.prompt.c_str(), item.target_window, abort_requested, item.request_id);

        g_request_storage.remove(item.request_id);
    }
}

typedef struct {
    bool keycode_state[512];
    std::vector<std::string> active_special_keys;
    int active_entry;
    Window target_window;
    std::thread recording_thread;
    std::unique_ptr<std::promise<record_state*>> recording_promise;
    config* cfg;
    bool remember_window;
    Display* display;
    int xi_opcode;
    std::thread worker_thread;
    int raw_keycode;
} app_state;

static void init_app_state(app_state* state) {
    memset(state->keycode_state, 0, sizeof(state->keycode_state));
    state->active_entry = -1;
    state->target_window = None;
}

static void handle_escape(app_state* state, XIRawEvent* raw) {
    int keycode = XKeysymToKeycode(state->display, XK_Escape);
    if (raw->detail == keycode) {
        if (debug_enabled) printf("Debug: ESC key detected (keycode=%d), recording_active=%d, pending_requests=%zu\n", keycode, recording_active.load(), g_request_storage.pending_count()); fflush(stdout);
        if (recording_active.load()) {
            if (debug_enabled) printf("Debug: ESC setting recording_active=false\n");
            recording_active.store(false);
        }
        g_request_storage.cancel_all();
        if (debug_enabled) printf("Debug: ESC cancel_all() done\n"); fflush(stdout);
    }
}

static void draw_vu_if_recording() {
    if (recording_active.load()) {
        screen_draw_vu_meter(current_volume.load());
        screen_refresh();
    }
}

static bool check_hotkey_match(app_state* state) {
    int idx = 0;
    for (const auto& entry : state->cfg->entries) {
        bool all_pressed = true;
        for (const auto& key : entry.keys) {
            KeySym target_keysym = config_key_to_keysym(key.c_str());
            KeyCode target_code = XKeysymToKeycode(state->display, target_keysym);

            if (target_code == 0 || target_code >= 512 || !state->keycode_state[target_code]) {
                all_pressed = false;
                break;
            }
        }

        if (all_pressed) {
            recording_active.store(true);
            vu_thread_start();
            state->active_special_keys = entry.special_keys;
            state->active_entry = idx;
            if (state->remember_window) {
                state->target_window = None;
            } else {
                state->target_window = get_active_window(state->display);
            }

            if (debug_enabled) printf("Debug: all keys pressed! starting recording, target=0x%lx\n", static_cast<unsigned long>(state->target_window));
            state->recording_promise = std::make_unique<std::promise<record_state*>>();
            state->recording_thread = std::thread(record_thread, state->recording_promise.get());
            return true;
        }
        idx++;
    }
    return false;
}

static void handle_key_press(app_state* state, XIRawEvent* raw) {
    int keycode = raw->detail;
    if (keycode < 512) state->keycode_state[keycode] = true;
    held_key_count.fetch_add(1, std::memory_order_relaxed);

    if (!recording_active.load()) {
        check_hotkey_match(state);
    }
    handle_escape(state, raw);
}

static bool is_active_key_released(app_state* state, int keycode) {
    for (const auto& key : state->cfg->entries[state->active_entry].keys) {
        KeySym target_keysym = config_key_to_keysym(key.c_str());
        KeyCode target_code = XKeysymToKeycode(state->display, target_keysym);
        if (target_code == keycode) {
            return true;
        }
    }
    return false;
}

static void queue_transcription(app_state* state, struct record_state* res) {
    queue_item item;
    item.buffer.assign(res->buffer.begin(), res->buffer.end());
    item.special_keys = state->active_special_keys;
    item.prompt = state->cfg->entries[state->active_entry].prompt;
    item.target_window = state->target_window;
    item.request_id = g_request_storage.add_request();

    transcription_queue.push(item);

    delete res;
}

static void handle_key_release(app_state* state, struct record_state* res) {
    {
        std::lock_guard<std::mutex> lock(screen_mutex);
        screen_print(0, "Finished. Captured %zu samples.", res->total);
    }

    queue_transcription(state, res);

    state->active_special_keys.clear();
    state->active_entry = -1;
}

static void process_xi_event(app_state* state, Display* dpy, XGenericEventCookie* cookie) {
    if (cookie->extension != state->xi_opcode || !XGetEventData(dpy, cookie)) return;

    XIRawEvent* raw = reinterpret_cast<XIRawEvent*>(cookie->data);
    int keycode = raw->detail;

    if (cookie->evtype == XI_RawKeyPress) {
        handle_key_press(state, raw);
    } else if (cookie->evtype == XI_RawKeyRelease) {
        if (keycode < 512) state->keycode_state[keycode] = false;
        held_key_count.fetch_sub(1, std::memory_order_relaxed);

        if (recording_active.load() && state->active_entry != -1) {
            if (is_active_key_released(state, keycode)) {
                state->raw_keycode = keycode;
                recording_active.store(false);
                current_volume.store(0.0f);
                {
                    std::lock_guard<std::mutex> lock(screen_mutex);
                    screen_draw_vu_meter(0.0f);
                }
                vu_thread_stop();
                vu_thread_wait();
                auto future = state->recording_promise->get_future();
                record_state* res = future.get();
                handle_key_release(state, res);
                state->recording_thread.join();
            }
        }
    }
    XFreeEventData(dpy, cookie);
}

int main(int argc, char* argv[]) {
    bool remember_window = false;
    parse_args(argc, argv, &remember_window);

    Display* dpy = nullptr;
    int xi_opcode = init_x11(&dpy);

    config* cfg = load_or_create_config();
    if (!cfg) return 1;

    init_screen(cfg);

    app_state state;
    init_app_state(&state);
    state.cfg = cfg;
    state.remember_window = remember_window;
    state.display = dpy;
    state.xi_opcode = xi_opcode;

    state.worker_thread = std::thread(worker_thread_func);

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        draw_vu_if_recording();
        if (resize_pending) {
            resize_pending = 0;
            if (g_cfg_for_resize) {
                std::lock_guard<std::mutex> lock(screen_mutex);
                screen_handle_resize(g_cfg_for_resize);
            }
        }
        if (ev.type == GenericEvent && ev.xcookie.extension == state.xi_opcode) {
            process_xi_event(&state, dpy, &ev.xcookie);
        }
    }

    abort_requested.store(true);
    transcription_queue.push(queue_item{});
    state.worker_thread.join();

    screen_cleanup();
    XCloseDisplay(dpy);
    return 0;
}
