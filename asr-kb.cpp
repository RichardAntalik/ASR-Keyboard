#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
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

#define SERVER_URL "http://127.0.0.1:8000/transcribe"
#define MAX_AUDIO_SIZE (1024 * 8192)
#define MAX_SOURCE_NAME 256
#define MAX_SOURCES 64
#define MAX_PROMPT 512
#define MAX_SHORTCUT_KEYS 8

std::atomic<bool> recording_active{false};
char source_name[MAX_SOURCE_NAME] = "";
bool debug_enabled{false};
char config_path[MAX_PROMPT] = "";

static KeySym config_key_to_keysym(const char* name) {
    if (strcmp(name, "ctrl") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(ctrl)=%ld\n", (long)XK_Control_L); return XK_Control_L; }
    if (strcmp(name, "super") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(super)=%ld\n", (long)XK_Super_L); return XK_Super_L; }
    if (strcmp(name, "alt") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(alt)=%ld\n", (long)XK_Alt_L); return XK_Alt_L; }
    if (strcmp(name, "space") == 0) { if (debug_enabled) printf("Debug: config_key_to_keysym(space)=%ld\n", (long)XK_space); return XK_space; }
    return XStringToKeysym(name);
}

#include "json.hpp"

struct record_state {
    short* buffer;
    size_t total;
};

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

// --- PulseAudio Listing Logic ---

typedef struct {
    int target_index;
    int current_count;
    bool done;
} list_context;

void source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    list_context *ctx = (list_context*)userdata;
    if (eol) { ctx->done = true; return; }
    
    if (ctx->target_index == -1) {
        printf("[%d] %s (%s)\n", ctx->current_count, i->description, i->name);
    } else if (ctx->current_count == ctx->target_index) {
        strncpy(source_name, i->name, MAX_SOURCE_NAME);
    }
    ctx->current_count++;
}

void run_pa_query(int index) {
    pa_mainloop* ml = pa_mainloop_new();
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "pa-query");
    list_context l_ctx = { .target_index = index, .current_count = 0, .done = false };
    pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (!l_ctx.done) {
        pa_mainloop_iterate(ml, 1, NULL);
        pa_context_state_t state = pa_context_get_state(ctx);
        if (state == PA_CONTEXT_READY) {
            static bool req = false;
            if (!req) { pa_context_get_source_info_list(ctx, source_info_cb, &l_ctx); req = true; }
        } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) break;
    }
    pa_context_unref(ctx); pa_mainloop_free(ml);
}

// --- Config Loading ---

config load_config() {
    config cfg = { .entry_count = 0 };

    struct stat st;
    if (stat(config_path, &st) != 0) {
        printf("Config file not found: %s\n", config_path);
        exit(1);
    }

    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        printf("Failed to open config file: %s\n", config_path);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    fread(content, 1, file_size, fp);
    content[file_size] = '\0';
    fclose(fp);

    nlohmann::json json_data = nlohmann::json::parse(content);
    free(content);

    cfg.entry_count = json_data.size();
    for (int i = 0; i < cfg.entry_count; i++) {
        auto& entry = json_data[i];
        cfg.entries[i].key_count = entry["shortcut"].size();
        for (int j = 0; j < cfg.entries[i].key_count; j++) {
            snprintf(cfg.entries[i].keys[j], 64, "%s", entry["shortcut"][j].get<std::string>().c_str());
        }
        snprintf(cfg.entries[i].prompt, MAX_PROMPT, "%s", entry["prompt"].get<std::string>().c_str());
        if (entry.contains("special_key") && entry["special_key"] != "null") {
            snprintf(cfg.entries[i].special_key, 64, "%s", entry["special_key"].get<std::string>().c_str());
        } else {
            snprintf(cfg.entries[i].special_key, 64, "null");
        }
    }

    // Sort entries by key_count (descending) to prevent subset overlap
    for (int i = 0; i < cfg.entry_count - 1; i++) {
        for (int j = i + 1; j < cfg.entry_count; j++) {
            if (cfg.entries[i].key_count < cfg.entries[j].key_count) {
                shortcut_entry temp = cfg.entries[i];
                cfg.entries[i] = cfg.entries[j];
                cfg.entries[j] = temp;
            }
        }
    }

    return cfg;
}

// --- PulseAudio Recording ---

void record_request_callback(pa_stream *p, size_t nbytes, void *userdata) {
    struct record_state *state = (struct record_state*)userdata;
    const void *data;
    if (pa_stream_peek(p, &data, &nbytes) < 0) return;
    
    if (data) {
        size_t samples = nbytes / sizeof(short);
        if (state->total + samples < MAX_AUDIO_SIZE) {
            memcpy(state->buffer + state->total, data, nbytes);
            state->total += samples;
        }
    }
    pa_stream_drop(p);
}

void* record_thread(void* arg) {
    pa_mainloop* ml = pa_mainloop_new();
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "asr-rec");
    pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (pa_context_get_state(ctx) != PA_CONTEXT_READY) pa_mainloop_iterate(ml, 1, NULL);

    struct record_state* state = (struct record_state*)malloc(sizeof(struct record_state));
    state->buffer = (short*)malloc(MAX_AUDIO_SIZE * sizeof(short));
    state->total = 0;

    pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = 16000, .channels = 1};
    pa_stream* s = pa_stream_new(ctx, "capture", &ss, NULL);
    pa_stream_set_read_callback(s, record_request_callback, state);
    pa_stream_connect_record(s, source_name[0] ? source_name : NULL, NULL, PA_STREAM_ADJUST_LATENCY);

    while (recording_active.load()) pa_mainloop_iterate(ml, 1, NULL);
    
    for(int i=0; i<15; i++) {
        usleep(10000);
        pa_mainloop_iterate(ml, 0, NULL);
    }

    pa_stream_disconnect(s);
    pa_stream_unref(s);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return (void*)state;
}

// --- Base64 and Server Logic ---

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

// --- Keyboard Simulation Logic ---

void simulate_key(Display* dpy, const char* keysym_name, bool shift) {
    KeyCode code = XKeysymToKeycode(dpy, XStringToKeysym(keysym_name));
    if (code == 0) return;

    if (shift) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, 0);
    XTestFakeKeyEvent(dpy, code, True, 0);
    XTestFakeKeyEvent(dpy, code, False, 0);
    if (shift) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, 0);
}

void type_text(const char* text, const char* special_key) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    for (const char* p = text; *p; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            char buf[2] = {c, 0};
            simulate_key(dpy, buf, false);
        } else if (c >= 'A' && c <= 'Z') {
            char buf[2] = {c + 32, 0};
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

    if (special_key && strcmp(special_key, "null") != 0 && strlen(special_key) > 0) {
        if (strcmp(special_key, "enter") == 0 || strcmp(special_key, "Return") == 0) {
            simulate_key(dpy, "Return", false);
        } else if (strcmp(special_key, "space") == 0 || strcmp(special_key, " ") == 0) {
            simulate_key(dpy, "space", false);
        } else if (strcmp(special_key, "tab") == 0 || strcmp(special_key, "Tab") == 0) {
            simulate_key(dpy, "Tab", false);
        } else {
            simulate_key(dpy, special_key, false);
        }
    }

    XFlush(dpy);
    XCloseDisplay(dpy);
}

void send_to_server(short* buffer, size_t size, const char* special_key, const char* prompt) {
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
                type_text(transcript.c_str(), special_key);
            } catch (...) {
                type_text(resp, special_key);
            }
        }
        free(json); curl_slist_free_all(h); curl_easy_cleanup(curl);
    }
}

// --- Main ---

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
            snprintf(config_path, sizeof(config_path), "%s", argv[i+1]);
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

    if (config_path[0] == '\0') {
        const char* home_dir = getenv("HOME");
        if (home_dir) {
            snprintf(config_path, sizeof(config_path), "%s/.config/asr-kb/config.json", home_dir);
        } else {
            char cwd[256];
            getcwd(cwd, sizeof(cwd));
            snprintf(config_path, sizeof(config_path), "%s/.config/asr-kb/config.json", cwd);
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

    config cfg = load_config();

    bool keycode_state[512] = {false};
    char active_special_key[64] = "null";
    int active_entry = -1;
    pthread_t thread_id;

    for (int i = 0; i < cfg.entry_count; i++) {
        printf("Shortcut: %s", cfg.entries[i].keys[0]);
        for (int j = 1; j < cfg.entries[i].key_count; j++) {
            printf("+%s", cfg.entries[i].keys[j]);
        }
        printf(" -> prompt: %s, special: %s\n", cfg.entries[i].prompt, cfg.entries[i].special_key);
    }

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == GenericEvent && ev.xcookie.extension == xi_opcode && XGetEventData(dpy, &ev.xcookie)) {
            XIRawEvent *raw = (XIRawEvent*)ev.xcookie.data;
            int keycode = raw->detail;

            if (ev.xcookie.evtype == XI_RawKeyPress) {
                if (debug_enabled) printf("Debug: XI_RawKeyPress keycode=%d\n", keycode);
                
                if (keycode < 512) keycode_state[keycode] = true;

                if (!recording_active.load()) {
                    for (int i = 0; i < cfg.entry_count; i++) {
                        bool all_pressed = true;
                        for (int j = 0; j < cfg.entries[i].key_count; j++) {
                            KeySym target_keysym = config_key_to_keysym(cfg.entries[i].keys[j]);
                            KeyCode target_code = XKeysymToKeycode(dpy, target_keysym);
                            
                            if (target_code == 0 || target_code >= 512 || !keycode_state[target_code]) {
                                all_pressed = false;
                                break;
                            }
                        }
                        
                        if (all_pressed) {
                            recording_active.store(true);
                            snprintf(active_special_key, sizeof(active_special_key), "%s", cfg.entries[i].special_key);
                            active_entry = i;
                            
                            if (debug_enabled) printf("Debug: all keys pressed! starting recording\n");
                            pthread_create(&thread_id, NULL, record_thread, NULL);
                            break; 
                        }
                    }
                }
            } else if (ev.xcookie.evtype == XI_RawKeyRelease) {
                if (debug_enabled) printf("Debug: XI_RawKeyRelease keycode=%d\n", keycode);
                if (keycode < 512) keycode_state[keycode] = false;

                if (recording_active.load() && active_entry != -1) {
                    bool active_released = false;
                    for (int j = 0; j < cfg.entries[active_entry].key_count; j++) {
                        KeySym target_keysym = config_key_to_keysym(cfg.entries[active_entry].keys[j]);
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
                        
                        send_to_server(res->buffer, res->total, active_special_key, cfg.entries[active_entry].prompt);
                        
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