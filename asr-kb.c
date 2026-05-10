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

#define SERVER_URL "http://127.0.0.1:8000/transcribe_c"
#define MAX_AUDIO_SIZE (1024 * 8192)
#define MAX_SOURCE_NAME 256
#define MAX_SOURCES 64

volatile bool recording_active = false;
char source_name[MAX_SOURCE_NAME] = "";

struct record_state {
    short* buffer;
    size_t total;
    pa_mainloop* mainloop;
    pa_context* ctx;
    pa_stream* stream;
};

// --- Base64 and Server Logic ---

char* base64_encode(short* buffer, size_t size) {
    static char result[MAX_AUDIO_SIZE * 2 * 4 / 3 + 1];
    const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j = 0;
    unsigned char* bytes = (unsigned char*)buffer;
    size_t byte_count = size * 2;
    for (i = 0; i < byte_count; i += 3) {
        unsigned char a = bytes[i];
        unsigned char b = (i + 1 < byte_count) ? bytes[i + 1] : 0;
        unsigned char c = (i + 2 < byte_count) ? bytes[i + 2] : 0;
        result[j++] = table[a >> 2];
        result[j++] = table[(a & 3) << 4 | (b >> 4)];
        result[j++] = (i + 1 < byte_count) ? table[(b & 15) << 2 | (c >> 6)] : '=';
        result[j++] = (i + 2 < byte_count) ? table[c & 63] : '=';
    }
    result[j] = '\0';
    return result;
}

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    char* dest = (char*)userdata;
    size_t offset = strlen(dest);
    memcpy(dest + offset, contents, total);
    dest[offset + total] = '\0';
    return total;
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

void type_text(const char* text, bool press_enter) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return;

    for (const char* p = text; *p; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            char buf[2] = {c, 0};
            simulate_key(dpy, buf, false);
        } else if (c >= 'A' && c <= 'Z') {
            char buf[2] = {c + 32, 0}; // Lowercase keysym + Shift
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

    if (press_enter) simulate_key(dpy, "Return", false);

    XFlush(dpy);
    XCloseDisplay(dpy);
}

void send_to_server(short* buffer, size_t size, bool enter_mode) {
    char* encoded = base64_encode(buffer, size);
    CURL *curl = curl_easy_init();
    if(curl) {
        printf("Sending audio to server (%zu samples)...\n", size);
        size_t json_size = (size * 3) + 512;
        char* json = malloc(json_size);
        snprintf(json, json_size, "{\"audio\": \"%s\", \"rate\": 16000}", encoded);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        char* response_text = calloc(1, 16384);
        curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL);
        curl_easy_setopt(curl, CURLOPT_POST, 1);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_text);

        CURLcode res = curl_easy_perform(curl);
        if(res == CURLE_OK) {
            printf("Server response: %s\n", response_text);
            type_text(response_text, enter_mode);
        } else {
            fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(res));
        }

        free(json);
        free(response_text);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

// --- PulseAudio Callbacks ---

void record_request_callback(pa_stream *p, size_t nbytes, void *userdata) {
    struct record_state *state = (struct record_state*)userdata;
    const void *data;
    if (pa_stream_peek(p, &data, &nbytes) >= 0 && data) {
        if (state->total + (nbytes / sizeof(short)) < MAX_AUDIO_SIZE) {
            memcpy(state->buffer + state->total, data, nbytes);
            state->total += nbytes / sizeof(short);
        }
        pa_stream_drop(p);
    }
}

// --- Recording Thread ---

void* record_thread(void* arg) {
    pa_mainloop* mainloop = pa_mainloop_new();
    pa_mainloop_api* api = pa_mainloop_get_api(mainloop);
    pa_context* ctx = pa_context_new(api, "asr-kb");

    pa_context_connect(ctx, NULL, 0, NULL);
    while (pa_context_get_state(ctx) != PA_CONTEXT_READY) pa_mainloop_iterate(mainloop, 1, NULL);

    struct record_state state;
    state.buffer = malloc(MAX_AUDIO_SIZE * sizeof(short));
    state.total = 0;

    pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = 16000, .channels = 1};
    state.stream = pa_stream_new(ctx, "capture", &ss, NULL);
    pa_buffer_attr attr = { .maxlength = -1, .fragsize = pa_usec_to_bytes(20000, &ss) };

    pa_stream_set_read_callback(state.stream, record_request_callback, &state);
    pa_stream_connect_record(state.stream, source_name, &attr, PA_STREAM_ADJUST_LATENCY);

    while (pa_stream_get_state(state.stream) != PA_STREAM_READY) pa_mainloop_iterate(mainloop, 1, NULL);

    printf("Recording...\n");
    while (recording_active && state.total < MAX_AUDIO_SIZE) pa_mainloop_iterate(mainloop, 1, NULL);

    // Drain
    for (int i = 0; i < 5; i++) {
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
        while (pa_mainloop_iterate(mainloop, 0, NULL) > 0);
    }

    pa_stream_disconnect(state.stream);
    pa_stream_unref(state.stream);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(mainloop);

    struct record_state* result = malloc(sizeof(struct record_state));
    result->buffer = state.buffer;
    result->total = state.total;
    return (void*)result;
}

// --- Main ---

int main(int argc, char* argv[]) {
    if (argc >= 3 && strcmp(argv[1], "-s") == 0) {
        snprintf(source_name, MAX_SOURCE_NAME, "%s", argv[2]);
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

    int space_keycode = XKeysymToKeycode(dpy, XK_space);
    int super_l = XKeysymToKeycode(dpy, XK_Super_L);
    int ctrl_l = XKeysymToKeycode(dpy, XK_Control_L);
    int alt_l = XKeysymToKeycode(dpy, XK_Alt_L);

    bool super_pressed = false, ctrl_pressed = false, alt_pressed = false;
    bool enter_mode = false;
    pthread_t thread_id;

    printf("Ready! \nCtrl+Super+Space: Record\nCtrl+Super+Alt+Space: Record + Enter\n");

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == GenericEvent && ev.xcookie.extension == xi_opcode && XGetEventData(dpy, &ev.xcookie)) {
            XIRawEvent *raw = (XIRawEvent*)ev.xcookie.data;
            int keycode = raw->detail;

            if (ev.xcookie.evtype == XI_RawKeyPress) {
                if (keycode == super_l) super_pressed = true;
                if (keycode == ctrl_l) ctrl_pressed = true;
                if (keycode == alt_l) alt_pressed = true;

                if (keycode == space_keycode && super_pressed && ctrl_pressed && !recording_active) {
                    recording_active = true;
                    enter_mode = alt_pressed;
                    pthread_create(&thread_id, NULL, record_thread, NULL);
                }
            } else if (ev.xcookie.evtype == XI_RawKeyRelease) {
                if (keycode == super_l) super_pressed = false;
                if (keycode == ctrl_l) ctrl_pressed = false;
                if (keycode == alt_l) alt_pressed = false;

                if (keycode == space_keycode && recording_active) {
                    recording_active = false;
                    void* ret;
                    pthread_join(thread_id, &ret);
                    struct record_state* res = (struct record_state*)ret;
                    send_to_server(res->buffer, res->total, enter_mode);
                    free(res->buffer);
                    free(res);
                }
            }
            XFreeEventData(dpy, &ev.xcookie);
        }
    }

    XCloseDisplay(dpy);
    return 0;
}