#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>

#include "json.hpp"
#include "config-parsing.h"
#include "pulse-recording.h"
#include "keyboard-sim.h"

#define SERVER_URL "http://127.0.0.1:8000/transcribe"

std::atomic<bool> recording_active{false};
bool debug_enabled{false};
char* config_path = nullptr;



char* base64_encode(short* buffer, size_t size) {
    static char result[MAX_AUDIO_SIZE * 3];
    const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j = 0;
    unsigned char* bytes = (unsigned char*)buffer;
    for (i = 0; i < size * 2; i += 3) {
        unsigned char a = bytes[i], b = (i+1 < size*2) ? bytes[i+1] : 0, c = (i+2 < size*2) ? bytes[i+2] : 0;
        result[j++] = table[a >> 2];
        result[j++] = table[(a & 3) << 4 | (b >> 4)];
        result[j++] = (i + 1 < size*2) ? table[(b & 15) << 2 | (c >> 6)] : '=';
        result[j++] = (i + 2 < size*2) ? table[c & 63] : '=';
    }
    result[j] = '\0';
    return result;
}

size_t write_cb(void* ptr, size_t size, size_t nmemb, void* data) {
    memcpy((char*)data + strlen((char*)data), (char*)ptr, size * nmemb);
    return size * nmemb;
}

size_t header_discard_cb(void* ptr, size_t size, size_t nmemb, void* data) {
    return size * nmemb;
}

void send_to_server(short* buffer, size_t size, const char* const* special_keys, int special_key_count, const char* prompt) {
    if (size == 0) return;
    CURL *curl = curl_easy_init();
    if(curl) {
        char* encoded = base64_encode(buffer, size);
        size_t jlen = (size * 3) + 1024;
        char* json = (char*)malloc(jlen);
        snprintf(json, jlen, "{\"audio_bytes\": \"%s\", \"sample_rate\": 16000, \"prompt\": \"%s\"}", encoded, prompt);
        struct curl_slist* h = curl_slist_append(NULL, "Content-Type: application/json");
        char resp[16384] = {0};
        curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_discard_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
        if(curl_easy_perform(curl) == CURLE_OK) {
            try {
                nlohmann::json json_resp = nlohmann::json::parse(resp);
                std::string transcript = json_resp.value("transcript", "");
                type_text(transcript.c_str(), (const char* const*)special_keys, special_key_count);
            } catch (...) {
                type_text(resp, (const char* const*)special_keys, special_key_count);
            }
        }
        free(json); curl_slist_free_all(h); curl_easy_cleanup(curl);
    }
}

int main(int argc, char* argv[]) {
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
        if (strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [-l] [-i <index>] [-d] [-c <config.json>] [-h]\n", argv[0]);
            printf("  -l    list audio sources\n");
            printf("  -i <index> select audio source by index\n");
            printf("  -d    enable debug output\n");
            printf("  -c <config.json> specify config file\n");
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
            getcwd(cwd, sizeof(cwd));
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

    bool keycode_state[512] = {false};
    char** active_special_keys = nullptr;
    int active_special_key_count = 0;
    int active_entry = -1;
    pthread_t thread_id;

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
                            
                            if (debug_enabled) printf("Debug: all keys pressed! starting recording\n");
                            pthread_create(&thread_id, NULL, record_thread, NULL);
                            break; 
                        }
                    }
                }
            } else if (ev.xcookie.evtype == XI_RawKeyRelease) {
                if (keycode < 512) keycode_state[keycode] = false;

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
                        
                         send_to_server(res->buffer, res->total, (const char* const*)active_special_keys, active_special_key_count, cfg->entries[active_entry].prompt);
                        
                         free(res->buffer); 
                         free(res);
                         active_entry = -1;
                    }
                }
            }
            XFreeEventData(dpy, &ev.xcookie);
        }
    }

    XCloseDisplay(dpy);
    return 0;
}