#include "config-parsing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

config* load_config(const char* path) {
    config* cfg = (config*)malloc(sizeof(config));
    cfg->entry_count = 0;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Config file not found: %s\n", path);
        exit(1);
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        printf("Failed to open config file: %s\n", path);
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

    cfg->entry_count = json_data.size();
    cfg->entries = (shortcut_entry*)malloc(cfg->entry_count * sizeof(shortcut_entry));

    for (int i = 0; i < cfg->entry_count; i++) {
        auto& entry = json_data[i];
        cfg->entries[i].key_count = entry["shortcut"].size();
        cfg->entries[i].keys = (char**)malloc(cfg->entries[i].key_count * sizeof(char*));
        for (int j = 0; j < cfg->entries[i].key_count; j++) {
            cfg->entries[i].keys[j] = strdup(entry["shortcut"][j].get<std::string>().c_str());
        }
        cfg->entries[i].prompt = strdup(entry["prompt"].get<std::string>().c_str());
        if (entry.contains("special_key")) {
            if (entry["special_key"].is_array()) {
                cfg->entries[i].special_key_count = entry["special_key"].size();
                cfg->entries[i].special_keys = (char**)malloc(cfg->entries[i].special_key_count * sizeof(char*));
                for (int j = 0; j < cfg->entries[i].special_key_count; j++) {
                    cfg->entries[i].special_keys[j] = strdup(entry["special_key"][j].get<std::string>().c_str());
                }
            } else {
                cfg->entries[i].special_key_count = 1;
                cfg->entries[i].special_keys = (char**)malloc(sizeof(char*));
                cfg->entries[i].special_keys[0] = strdup(entry["special_key"].get<std::string>().c_str());
            }
        } else {
            cfg->entries[i].special_key_count = 0;
            cfg->entries[i].special_keys = nullptr;
        }
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
