#include "client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.hpp"
#include "pulse-recording.h"
#include "keyboard-sim.h"

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
