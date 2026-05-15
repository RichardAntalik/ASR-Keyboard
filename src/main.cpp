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

#include "json.hpp"
#include "config-parsing.h"
#include "pulse-recording.h"
#include "keyboard-sim.h"
#include "client.h"
#include "queue.h"
#include "request-storage.h"

std::atomic<bool> recording_active{false};
std::atomic<bool> abort_requested{false};
std::atomic<int> held_key_count{0};
bool debug_enabled{false};
char* config_path = nullptr;

thread_safe_queue transcription_queue;

void* worker_thread_func(void* arg) {
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

int main(int argc, char* argv[]) {
    bool remember_window = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            run_pa_query(-1);
            return 0;
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
            remember_window = true;
        }
        if (strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [-l] [-i <index>] [-d] [-c <config.json>] [-a] [-h]\n", argv[0]);
            printf("  -l    list audio sources\n");
            printf("  -i <index> select audio source by index\n");
            printf("  -d    enable debug output\n");
            printf("  -c <config.json> specify config file\n");
            printf("  -a    remember the active window for typing\n");
            printf("  -h    show help\n");
            return 0;
        }
    }

    if (config_path == nullptr) {
        const char* home_dir = getenv("HOME");
        if (home_dir) {
            config_path = strdup(home_dir);
            char* new_path = (char*)malloc(strlen(config_path) + 32);
            snprintf(new_path, strlen(config_path) + 32, "%s/.config/asr-kb/config.json", config_path);
            free(config_path);
            config_path = new_path;
        } else {
            char cwd[256];
            if (!getcwd(cwd, sizeof(cwd))) return 1;
            config_path = strdup(cwd);
            char* new_path = (char*)malloc(strlen(config_path) + 32);
            snprintf(new_path, strlen(config_path) + 32, "%s/.config/asr-kb/config.json", config_path);
            free(config_path);
            config_path = new_path;
        }
    }

    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    int xi_opcode, event, error;
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error)) return 1;

    XIEventMask evmask;
    unsigned char mask[XIMaskLen(XI_LASTEVENT)] = { 0 };
    evmask.deviceid = XIAllMasterDevices;
    evmask.mask_len = sizeof(mask);
    evmask.mask = mask;
    XISetMask(mask, XI_RawKeyPress);
    XISetMask(mask, XI_RawKeyRelease);
    XISelectEvents(dpy, DefaultRootWindow(dpy), &evmask, 1);

    config* cfg = load_config(config_path);

    if (cfg == nullptr) {
        printf("Config file not found at %s\n", config_path);
        printf("Would you like to create a default configuration file? (y/n): ");
        char response = getchar();
        if (response == 'y' || response == 'Y') {
            if (create_default_config(config_path)) {
                printf("Default configuration created successfully at %s\n", config_path);
                cfg = load_config(config_path);
            } else {
                printf("Failed to create default configuration. Please create it manually at %s\n", config_path);
                return 1;
            }
        } else {
            printf("Exiting without configuration.\n");
            return 1;
        }
    }

    if (!cfg) return 1;

    bool keycode_state[512] = {false};
    char** active_special_keys = nullptr;
    int active_special_key_count = 0;
    int active_entry = -1;
    Window target_window = None;
    pthread_t thread_id;
    pthread_t worker_thread;
    pthread_create(&worker_thread, NULL, worker_thread_func, NULL);

    for (int i = 0; i < cfg->entry_count; i++) {
        printf("Shortcut: %s", cfg->entries[i].keys[0]);
        for (int j = 1; j < cfg->entries[i].key_count; j++) {
            printf("+%s", cfg->entries[i].keys[j]);
        }
        printf(" -> prompt: %s, special: ", cfg->entries[i].prompt);
        for (int j = 0; j < cfg->entries[i].special_key_count; j++) {
            printf("%s", cfg->entries[i].special_keys[j]);
            if (j + 1 < cfg->entries[i].special_key_count) printf("+");
        }
        printf("\n");
    }

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == GenericEvent && ev.xcookie.extension == xi_opcode && XGetEventData(dpy, &ev.xcookie)) {
            XIRawEvent *raw = (XIRawEvent*)ev.xcookie.data;
            int keycode = raw->detail;

            if (ev.xcookie.evtype == XI_RawKeyPress) {
                if (keycode < 512) keycode_state[keycode] = true;
                held_key_count.fetch_add(1, std::memory_order_relaxed);

                if (!recording_active.load()) {
                    for (int i = 0; i < cfg->entry_count; i++) {
                        bool all_pressed = true;
                        for (int j = 0; j < cfg->entries[i].key_count; j++) {
                            KeySym target_keysym = config_key_to_keysym(cfg->entries[i].keys[j]);
                            KeyCode target_code = XKeysymToKeycode(dpy, target_keysym);
                            
                            if (target_code == 0 || target_code >= 512 || !keycode_state[target_code]) {
                                all_pressed = false;
                                break;
                            }
                        }
                        
                        if (all_pressed) {
                            recording_active.store(true);
                            active_special_key_count = cfg->entries[i].special_key_count;
                            if (active_special_key_count > 0) {
                                active_special_keys = (char**)malloc(active_special_key_count * sizeof(char*));
                                for (int j = 0; j < active_special_key_count; j++) {
                                    active_special_keys[j] = strdup(cfg->entries[i].special_keys[j]);
                                }
                            }
                            active_entry = i;
                            if (remember_window) {
                                target_window = get_active_window(dpy);
                            } else {
                                target_window = None;
                            }
                            
                            if (debug_enabled) printf("Debug: all keys pressed! starting recording, target=0x%lx\n", (unsigned long)target_window);
                            pthread_create(&thread_id, NULL, record_thread, NULL);
                            break; 
                        }
                    }
                }

                if (keycode == XKeysymToKeycode(dpy, XK_Escape)) {
                    if (debug_enabled) printf("Debug: ESC key detected (keycode=%d), recording_active=%d, pending_requests=%zu\n", keycode, recording_active.load(), g_request_storage.pending_count()); fflush(stdout);
                    if (recording_active.load()) {
                        if (debug_enabled) printf("Debug: ESC setting recording_active=false\n");
                        recording_active.store(false);
                    }
                    g_request_storage.cancel_all();
                    if (debug_enabled) printf("Debug: ESC cancel_all() done\n"); fflush(stdout);
                }
            } else if (ev.xcookie.evtype == XI_RawKeyRelease) {
                if (keycode < 512) keycode_state[keycode] = false;
                held_key_count.fetch_sub(1, std::memory_order_relaxed);

                if (recording_active.load() && active_entry != -1) {
                    bool active_released = false;
                    for (int j = 0; j < cfg->entries[active_entry].key_count; j++) {
                        KeySym target_keysym = config_key_to_keysym(cfg->entries[active_entry].keys[j]);
                        KeyCode target_code = XKeysymToKeycode(dpy, target_keysym);
                        if (target_code == keycode) {
                            active_released = true;
                            break;
                        }
                    }

                    if (active_released) {
                        recording_active.store(false);
                        void* ret;
                        pthread_join(thread_id, &ret);
                        struct record_state* res = (struct record_state*)ret;
                        printf("Finished. Captured %zu samples.\n", res->total);
                        
                        queue_item item;
                        item.buffer = res->buffer;
                        item.size = res->total;
                        item.special_key_count = active_special_key_count;
                        item.special_keys = (char**)malloc(active_special_key_count * sizeof(char*));
                        for(int k=0; k<active_special_key_count; k++) {
                            item.special_keys[k] = strdup(active_special_keys[k]);
                        }
                        item.prompt = cfg->entries[active_entry].prompt;
                        item.target_window = target_window;
                        item.request_id = g_request_storage.add_request();
                        
                        transcription_queue.push(item);
                        
                        free(res);
                    }
                }
            }
            XFreeEventData(dpy, &ev.xcookie);
        }
    }

    abort_requested.store(true);
    transcription_queue.push(queue_item{nullptr, 0, nullptr, 0, nullptr, None, 0});
    pthread_join(worker_thread, nullptr);

    XCloseDisplay(dpy);
    return 0;
}
