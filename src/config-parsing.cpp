#include "config-parsing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

bool create_default_config(const char* path) {
    char dir_path[512];
    strncpy(dir_path, path, sizeof(dir_path));
    char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Recursive directory creation is not trivial in C, 
        // but for ~/.config/asr-kb we can try to create it.
        // We'll assume the user might need to do this or we try a simple approach.
        // For simplicity and robustness, we use a system call or multiple mkdirs.
        // But better yet, let's just try to open the file first.
    }

    const char* default_content = R"([
  { "shortcut": ["ctrl", "super", "space"], "prompt": "transcribe the speech with proper punctuation and capitalization.", "special_key": "null" },
  { "shortcut": ["ctrl", "super", "alt", "space"], "prompt": "transcribe the speech with proper punctuation and capitalization.", "special_key": "enter" }
])";

    FILE* fp = fopen(path, "w");
    if (!fp) {
        perror("Failed to create config file");
        return false;
    }
    fputs(default_content, fp);
    fclose(fp);
    return true;
}

config* load_config(const char* path) {
    config* cfg = (config*)malloc(sizeof(config));
    cfg->entry_count = 0;

    struct stat st;
    if (stat(path, &st) != 0) {
        free(cfg);
        return nullptr;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        printf("Failed to open config file: %s\n", path);
        free(cfg);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    fread(content, 1, file_size, fp);
    content[file_size] = '\0';
    fclose(fp);

    nlohmann::json json_data;
    try {
        json_data = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        printf("Error parsing config file: %s\n", e.what());
        free(content);
        free(cfg);
        return nullptr;
    }
    free(content);

    try {
        cfg->entry_count = json_data.size();
        cfg->entries = (shortcut_entry*)calloc(cfg->entry_count, sizeof(shortcut_entry));

        for (int i = 0; i < cfg->entry_count; i++) {
            auto& entry = json_data[i];
            cfg->entries[i].key_count = entry["shortcut"].size();
            cfg->entries[i].keys = (char**)calloc(cfg->entries[i].key_count, sizeof(char*));
            for (int j = 0; j < cfg->entries[i].key_count; j++) {
                cfg->entries[i].keys[j] = strdup(entry["shortcut"][j].get<std::string>().c_str());
            }
            cfg->entries[i].prompt = strdup(entry["prompt"].get<std::string>().c_str());
            if (entry.contains("special_key")) {
                if (entry["special_key"].is_array()) {
                    cfg->entries[i].special_key_count = entry["special_key"].size();
                    cfg->entries[i].special_keys = (char**)calloc(cfg->entries[i].special_key_count, sizeof(char*));
                    for (int j = 0; j < cfg->entries[i].special_key_count; j++) {
                        cfg->entries[i].special_keys[j] = strdup(entry["special_key"][j].get<std::string>().c_str());
                    }
                } else {
                    cfg->entries[i].special_key_count = 1;
                    cfg->entries[i].special_keys = (char**)calloc(1, sizeof(char*));
                    cfg->entries[i].special_keys[0] = strdup(entry["special_key"].get<std::string>().c_str());
                }
            } else {
                cfg->entries[i].special_key_count = 0;
                cfg->entries[i].special_keys = nullptr;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        printf("Config structure error: %s\n", e.what());
        free_config(cfg);
        return nullptr;
    }

    for (int i = 0; i < cfg->entry_count - 1; i++) {
        for (int j = i + 1; j < cfg->entry_count; j++) {
            if (cfg->entries[i].key_count < cfg->entries[j].key_count) {
                shortcut_entry temp = cfg->entries[i];
                cfg->entries[i] = cfg->entries[j];
                cfg->entries[j] = temp;
            }
        }
    }
    return cfg;
}

void free_config(config* cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->entry_count; i++) {
        for (int j = 0; j < cfg->entries[i].key_count; j++) {
            free(cfg->entries[i].keys[j]);
        }
        free(cfg->entries[i].keys);
        free(cfg->entries[i].prompt);
        for (int j = 0; j < cfg->entries[i].special_key_count; j++) {
            free(cfg->entries[i].special_keys[j]);
        }
        free(cfg->entries[i].special_keys);
    }
    free(cfg->entries);
    free(cfg);
}
