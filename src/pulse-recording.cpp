#include "pulse-recording.h"
#include "globals.h"
#include <cstdio>
#include <cstring>
#include <math.h>
#include <thread>
#include <chrono>

std::string source_name = "";
std::atomic<float> current_volume{0.0f};

typedef struct {
    int target_index;
    int current_count;
    bool done;
} list_context;

void source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    list_context *ctx = reinterpret_cast<list_context*>(userdata);
    if (eol) { ctx->done = true; return; }
    
    if (ctx->target_index == -1) {
        printf("[%d] %s (%s)\n", ctx->current_count, i->description, i->name);
    } else if (ctx->current_count == ctx->target_index) {
        source_name = i->name;
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
    struct record_state *state = reinterpret_cast<struct record_state*>(userdata);
    const void *data;
    if (pa_stream_peek(p, &data, &nbytes) < 0) return;
    
    if (data) {
        size_t samples = nbytes / sizeof(short);
        if (state->buffer.size() - state->total + samples <= MAX_AUDIO_SIZE) {
            state->buffer.insert(state->buffer.end(), reinterpret_cast<const short*>(data), reinterpret_cast<const short*>(data) + samples);
            state->total += samples;
        }
        
        const short* samples_ptr = reinterpret_cast<const short*>(data);
        double sum = 0.0;
        struct ptr_range { const short* b; const short* e; const short* begin() const { return b; } const short* end() const { return e; } };
        for (short s : ptr_range{samples_ptr, samples_ptr + samples}) {
            double val = (double)s / 32768.0;
            sum += val * val;
        }
        float rms = (float)sqrt(sum / (double)samples);
        
        if (rms < 0.001f) {
            current_volume.store(0.0f);
        } else {
            float db = 20.0f * log10f(rms);
            current_volume.store((db + 60.0f) / 60.0f);
        }
        float vol = current_volume.load();
        if (vol < 0.0f) current_volume.store(0.0f);
        if (vol > 1.0f) current_volume.store(1.0f);
    }
    pa_stream_drop(p);
}

static struct record_state* create_record_state(pa_context* ctx, pa_stream** out_stream) {
    struct record_state* state = new record_state();
    state->buffer.reserve(MAX_AUDIO_SIZE);

    pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = 16000, .channels = 1};
    pa_stream* s = pa_stream_new(ctx, "capture", &ss, NULL);
    pa_stream_set_read_callback(s, record_request_callback, state);

    pa_buffer_attr attr;
    attr.maxlength = -1;
    attr.tlength = 160;
    attr.prebuf = 0;
    attr.minreq = 10;
    attr.fragsize = 10;

    pa_stream_connect_record(s, !source_name.empty() ? source_name.c_str() : nullptr, &attr, PA_STREAM_ADJUST_LATENCY);
    *out_stream = s;

    return state;
}

static void run_record_loop(pa_mainloop* ml, pa_stream* s) {
    while (recording_active.load()) pa_mainloop_iterate(ml, 1, NULL);

    for(int i=0; i<15; i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(10000));
        pa_mainloop_iterate(ml, 0, NULL);
    }

    pa_stream_disconnect(s);
    pa_stream_unref(s);
}

static void cleanup_pa(pa_mainloop* ml, pa_context* ctx) {
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
}

void record_thread(std::promise<record_state*>* prom) {
    pa_mainloop* ml = pa_mainloop_new();
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "asr-rec");
    pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    while (pa_context_get_state(ctx) != PA_CONTEXT_READY) pa_mainloop_iterate(ml, 1, NULL);

    pa_stream* s = NULL;
    struct record_state* state = create_record_state(ctx, &s);

    run_record_loop(ml, s);
    cleanup_pa(ml, ctx);

    prom->set_value(state);
}
