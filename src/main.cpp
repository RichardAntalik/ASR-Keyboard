#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>
#include <csignal>

#include "json.hpp"
#include "config-parsing.h"
#include "pulse-recording.h"
#include "keyboard-sim.h"
#include "client.h"
#include "queue.h"
#include "screen-manager.h"
#include "request-storage.h"
#include "vu-thread.h"

std::atomic<bool> recording_active{false};
std::atomic<bool> abort_requested{false};
std::atomic<int> held_key_count{0};
bool debug_enabled{false};
char* config_path = nullptr;

thread_safe_queue transcription_queue;

static config* g_cfg_for_resize = nullptr;

extern void screen_handle_resize(const config* cfg);

static void sigwinch_handler(int) {
    if (g_cfg_for_resize) {
        pthread_mutex_lock(&screen_mutex);
        screen_handle_resize(g_cfg_for_resize);
        pthread_mutex_unlock(&screen_mutex);
    }
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
            config_path = strdup(argv[i+1]);
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

static void resolve_config_path() {
    if (config_path != nullptr) return;

    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    const char* home_dir = getenv("HOME");

    if (xdg_config) {
        char* new_path = (char*)malloc(strlen(xdg_config) + 20);
        snprintf(new_path, strlen(xdg_config) + 20, "%s/asr-kb/config.json", xdg_config);
        config_path = new_path;
    } else if (home_dir) {
        char* new_path = (char*)malloc(strlen(home_dir) + 20);
        snprintf(new_path, strlen(home_dir) + 20, "%s/.config/asr-kb/config.json", home_dir);
        config_path = new_path;
    } else {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd))) exit(1);
        config_path = strdup(cwd);
        char* new_path = (char*)malloc(strlen(config_path) + 20);
        snprintf(new_path, strlen(config_path) + 20, "%s/.config/asr-kb/config.json", config_path);
        free(config_path);
        config_path = new_path;
    }
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
    config* cfg = load_config(config_path);

    if (cfg == nullptr) {
        char response = getchar();
        if (response == 'y' || response == 'Y') {
            if (create_default_config(config_path)) {
                cfg = load_config(config_path);
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

static void* worker_thread_func(void* arg) {
    while (true) {
        queue_item item = transcription_queue.pop();
        if (item.buffer == nullptr) break;

        if (g_request_storage.is_cancelled(item.request_id)) {
            if (debug_enabled) printf("Debug: worker request %d cancelled, skipping\n", item.request_id);
            free(item.buffer);
            for (int k = 0; k < item.special_key_count; k++) {
                free(item.special_keys[k]);
            }
            free(item.special_keys);
            g_request_storage.remove(item.request_id);
            continue;
        }

        if (debug_enabled) printf("Debug: worker processing request %d, %zu samples\n", item.request_id, item.size);
        send_to_server(item.buffer, item.size, (const char* const*)item.special_keys, item.special_key_count, item.prompt, item.target_window, abort_requested, item.request_id);

        free(item.buffer);
        for (int k = 0; k < item.special_key_count; k++) {
            free(item.special_keys[k]);
        }
        free(item.special_keys);
        g_request_storage.remove(item.request_id);
    }
    return nullptr;
}

static void setup_worker_thread(pthread_t* worker_thread) {
    pthread_create(worker_thread, NULL, worker_thread_func, NULL);
}

typedef struct {
    bool keycode_state[512];
    char** active_special_keys;
    int active_special_key_count;
    int active_entry;
    Window target_window;
    pthread_t thread_id;
    config* cfg;
    bool remember_window;
    Display* display;
    int xi_opcode;
    pthread_t worker_thread;
    int raw_keycode;
} app_state;

static void init_app_state(app_state* state) {
    memset(state->keycode_state, 0, sizeof(state->keycode_state));
    state->active_special_keys = nullptr;
    state->active_special_key_count = 0;
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
        screen_draw_vu_meter(current_volume);
        screen_refresh();
    }
}

static bool check_hotkey_match(app_state* state) {
    for (int i = 0; i < state->cfg->entry_count; i++) {
        bool all_pressed = true;
        for (int j = 0; j < state->cfg->entries[i].key_count; j++) {
            KeySym target_keysym = config_key_to_keysym(state->cfg->entries[i].keys[j]);
            KeyCode target_code = XKeysymToKeycode(state->display, target_keysym);

            if (target_code == 0 || target_code >= 512 || !state->keycode_state[target_code]) {
                all_pressed = false;
                break;
            }
        }

        if (all_pressed) {
            recording_active.store(true);
            vu_thread_start();
            state->active_special_key_count = state->cfg->entries[i].special_key_count;
            if (state->active_special_key_count > 0) {
                state->active_special_keys = (char**)malloc(state->active_special_key_count * sizeof(char*));
                for (int j = 0; j < state->active_special_key_count; j++) {
                    state->active_special_keys[j] = strdup(state->cfg->entries[i].special_keys[j]);
                }
            }
            state->active_entry = i;
            if (state->remember_window) {
                state->target_window = None;
            } else {
                state->target_window = get_active_window(state->display);
            }

            if (debug_enabled) printf("Debug: all keys pressed! starting recording, target=0x%lx\n", (unsigned long)state->target_window);
            pthread_create(&state->thread_id, NULL, record_thread, NULL);
            return true;
        }
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
    for (int j = 0; j < state->cfg->entries[state->active_entry].key_count; j++) {
        KeySym target_keysym = config_key_to_keysym(state->cfg->entries[state->active_entry].keys[j]);
        KeyCode target_code = XKeysymToKeycode(state->display, target_keysym);
        if (target_code == keycode) {
            return true;
        }
    }
    return false;
}

static void queue_transcription(app_state* state, struct record_state* res) {
    queue_item item;
    item.buffer = res->buffer;
    item.size = res->total;
    item.special_key_count = state->active_special_key_count;
    item.special_keys = (char**)malloc(state->active_special_key_count * sizeof(char*));
    for(int k=0; k<state->active_special_key_count; k++) {
        item.special_keys[k] = strdup(state->active_special_keys[k]);
    }
    item.prompt = state->cfg->entries[state->active_entry].prompt;
    item.target_window = state->target_window;
    item.request_id = g_request_storage.add_request();

    transcription_queue.push(item);

    free(res);
}

static void handle_key_release(app_state* state, struct record_state* res) {
    pthread_mutex_lock(&screen_mutex);
    screen_print(0, "Finished. Captured %zu samples.", res->total);
    pthread_mutex_unlock(&screen_mutex);

    queue_transcription(state, res);

    free(state->active_special_keys);
    state->active_special_keys = nullptr;
    state->active_special_key_count = 0;
    state->active_entry = -1;
}

static void process_xi_event(app_state* state, Display* dpy, XGenericEventCookie* cookie) {
    if (cookie->extension != state->xi_opcode || !XGetEventData(dpy, cookie)) return;

    XIRawEvent* raw = (XIRawEvent*)cookie->data;
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
                current_volume = 0.0f;
                pthread_mutex_lock(&screen_mutex);
                screen_draw_vu_meter(0.0f);
                pthread_mutex_unlock(&screen_mutex);
                vu_thread_stop();
                vu_thread_wait();
                void* ret;
                pthread_join(state->thread_id, &ret);
                struct record_state* res = (struct record_state*)ret;
                handle_key_release(state, res);
            }
        }
    }
    XFreeEventData(dpy, cookie);
}

int main(int argc, char* argv[]) {
    bool remember_window = false;
    parse_args(argc, argv, &remember_window);
    resolve_config_path();

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

    setup_worker_thread(&state.worker_thread);

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        draw_vu_if_recording();
        if (ev.type == GenericEvent && ev.xcookie.extension == state.xi_opcode) {
            process_xi_event(&state, dpy, &ev.xcookie);
        }
    }

    abort_requested.store(true);
    transcription_queue.push(queue_item{nullptr, 0, nullptr, 0, nullptr, None, 0});
    pthread_join(state.worker_thread, nullptr);

    screen_cleanup();
    XCloseDisplay(dpy);
    return 0;
}
