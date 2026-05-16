#include "config-parsing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

bool create_default_config(const char* path) {
    const char* default_content = R"([
  { "shortcut": ["ctrl", "super", "space"], "prompt": "<|audio|>transcribe the speech with proper punctuation and capitalization.", "special_key": "null" },
  { "shortcut": ["ctrl", "super", "alt", "space"], "prompt": "<|audio|>transcribe the speech with proper punctuation and capitalization.", "special_key": "enter" }
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

static char* read_config_file(const char* path, long* out_size) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return nullptr;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        printf("Failed to open config file: %s\n", path);
        return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (fread(content, 1, file_size, fp) != file_size) {
        free(content);
        fclose(fp);
        return nullptr;
    }
    content[file_size] = '\0';
    fclose(fp);

    *out_size = file_size;
    return content;
}

static void free_shortcut_entry(shortcut_entry* entry) {
    for (int j = 0; j < entry->key_count; j++) {
        free(entry->keys[j]);
    }
    free(entry->keys);
    free(entry->prompt);
    for (int j = 0; j < entry->special_key_count; j++) {
        free(entry->special_keys[j]);
    }
    free(entry->special_keys);
}

static bool parse_entry(nlohmann::json& entry, shortcut_entry* parsed, int idx) {
    parsed->key_count = entry["shortcut"].size();
    parsed->keys = (char**)calloc(parsed->key_count, sizeof(char*));
    for (int j = 0; j < parsed->key_count; j++) {
        parsed->keys[j] = strdup(entry["shortcut"][j].get<std::string>().c_str());
    }
    parsed->prompt = strdup(entry["prompt"].get<std::string>().c_str());

    if (entry.contains("special_key")) {
        if (entry["special_key"].is_array()) {
            parsed->special_key_count = entry["special_key"].size();
            parsed->special_keys = (char**)calloc(parsed->special_key_count, sizeof(char*));
            for (int j = 0; j < parsed->special_key_count; j++) {
                parsed->special_keys[j] = strdup(entry["special_key"][j].get<std::string>().c_str());
            }
        } else {
            parsed->special_key_count = 1;
            parsed->special_keys = (char**)calloc(1, sizeof(char*));
            parsed->special_keys[0] = strdup(entry["special_key"].get<std::string>().c_str());
        }
    } else {
        parsed->special_key_count = 0;
        parsed->special_keys = nullptr;
    }

    return true;
}

static bool parse_config_json(const char* content, config* cfg) {
    nlohmann::json json_data;
    try {
        json_data = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        printf("Error parsing config file: %s\n", e.what());
        return false;
    }

    try {
        cfg->entry_count = json_data.size();
        cfg->entries = (shortcut_entry*)calloc(cfg->entry_count, sizeof(shortcut_entry));

        for (int i = 0; i < cfg->entry_count; i++) {
            if (!parse_entry(json_data[i], &cfg->entries[i], i)) {
                free_config(cfg);
                return false;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        printf("Config structure error: %s\n", e.what());
        free_config(cfg);
        return false;
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
    return true;
}

config* load_config(const char* path) {
    config* cfg = (config*)malloc(sizeof(config));
    cfg->entry_count = 0;

    long file_size;
    char* content = read_config_file(path, &file_size);
    if (!content) {
        free(cfg);
        return nullptr;
    }

    if (!parse_config_json(content, cfg)) {
        free(content);
        return nullptr;
    }

    free(content);
    return cfg;
}

void free_config(config* cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->entry_count; i++) {
        free_shortcut_entry(&cfg->entries[i]);
    }
    free(cfg->entries);
    free(cfg);
}
