#include "client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.hpp"
#include "pulse-recording.h"
#include "keyboard-sim.h"
#include "request-storage.h"

extern bool debug_enabled;

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

void send_to_server(short* buffer, size_t size, const char* const* special_keys, int special_key_count, const char* prompt, Window target_window, std::atomic<bool>& abort_requested, int request_id) {
    if (size == 0) return;
    CURL *curl = curl_easy_init();
    if(!curl) return;

    char* encoded = base64_encode(buffer, size);
    size_t jlen = (size * 3) + 2048;
    char* json = (char*)malloc(jlen);
    snprintf(json, jlen, "{\"audio_bytes\": \"%s\", \"sample_rate\": 16000, \"prompt\": \"%s\", \"request_id\": %d, \"target_window\": %lu}", encoded, prompt, request_id, (unsigned long)target_window);
    struct curl_slist* h = curl_slist_append(NULL, "Content-Type: application/json");
    char resp[16384] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, SERVER_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLM *multi = curl_multi_init();
    curl_multi_add_handle(multi, curl);

    int running = 0;
    do {
        if (abort_requested.load()) {
            curl_multi_remove_handle(multi, curl);
            break;
        }
        curl_multi_perform(multi, &running);
        curl_multi_poll(multi, nullptr, 0, 100, nullptr);
    } while (running > 0);

    if (!abort_requested.load()) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (debug_enabled) printf("Debug: HTTP response code=%ld, request_id=%d\n", http_code, request_id); fflush(stdout);
        if (http_code == 200) {
            try {
                nlohmann::json json_resp = nlohmann::json::parse(resp);
                std::string transcript = json_resp.value("transcript", "");
                if (debug_enabled) printf("Debug: transcript='%s', request_id=%d\n", transcript.c_str(), request_id); fflush(stdout);
                if (!transcript.empty()) {
                    bool cancelled = g_request_storage.is_cancelled(request_id);
                    if (debug_enabled) printf("Debug: is_cancelled(%d)=%d\n", request_id, cancelled); fflush(stdout);
                    if (!cancelled) {
                        Window response_window = (Window)json_resp.value("target_window", (long)target_window);
                        if (debug_enabled) printf("Debug: typing to window 0x%lx\n", (unsigned long)response_window); fflush(stdout);
                        type_text(transcript.c_str(), (const char* const*)special_keys, special_key_count, response_window, abort_requested, request_id);
                    } else {
                        if (debug_enabled) printf("Debug: request %d cancelled, skipping typing\n", request_id);
                    }
                }
            } catch (...) {
                // Don't type anything if parsing fails to avoid typing error messages
            }
        }
    }

    curl_easy_cleanup(curl);
    curl_multi_cleanup(multi);
    free(json);
    curl_slist_free_all(h);
}
