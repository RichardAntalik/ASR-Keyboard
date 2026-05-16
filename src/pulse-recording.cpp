#include "pulse-recording.h"
#include <cstdio>
#include <cstring>
#include <math.h>

extern std::atomic<bool> abort_requested;

char source_name[MAX_SOURCE_NAME] = "";
float current_volume = 0.0f;

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
        
        const short* samples_ptr = (const short*)data;
        double sum = 0.0;
        for (size_t i = 0; i < samples; i++) {
            double val = (double)samples_ptr[i] / 32768.0;
            sum += val * val;
        }
        float rms = (float)sqrt(sum / (double)samples);
        
        if (rms < 0.001f) {
            current_volume = 0.0f;
        } else {
            float db = 20.0f * log10f(rms);
            current_volume = (db + 60.0f) / 60.0f;
        }
        if (current_volume < 0.0f) current_volume = 0.0f;
        if (current_volume > 1.0f) current_volume = 1.0f;
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

    pa_buffer_attr attr;
    attr.maxlength = -1;
    attr.tlength = 160;      // 10ms of audio at 16kHz (16000 * 1 * 2 bytes)
    attr.prebuf = 0;
    attr.minreq = 10;        // minimum request in bytes
    attr.fragsize = 10;      // fragment size in bytes

    pa_stream_connect_record(s, source_name[0] ? source_name : NULL, &attr, PA_STREAM_ADJUST_LATENCY);

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
