#include "config-parsing.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <algorithm>

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

static bool parse_entry(nlohmann::json& entry, shortcut_entry& parsed) {
    for (auto& k : entry["shortcut"]) {
        parsed.keys.push_back(k.get<std::string>());
    }
    parsed.prompt = entry["prompt"].get<std::string>();

    if (entry.contains("special_key")) {
        if (entry["special_key"].is_array()) {
            for (auto& sk : entry["special_key"]) {
                parsed.special_keys.push_back(sk.get<std::string>());
            }
        } else {
            parsed.special_keys.push_back(entry["special_key"].get<std::string>());
        }
    }

    return true;
}

static bool parse_config_json(const char* content, config& cfg) {
    nlohmann::json json_data;
    try {
        json_data = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        printf("Error parsing config file: %s\n", e.what());
        return false;
    }

    try {
        for (auto& entry : json_data) {
            shortcut_entry parsed;
            if (!parse_entry(entry, parsed)) {
                return false;
            }
            cfg.entries.push_back(std::move(parsed));
        }
    } catch (const nlohmann::json::exception& e) {
        printf("Config structure error: %s\n", e.what());
        return false;
    }

    std::sort(cfg.entries.begin(), cfg.entries.end(), [](const shortcut_entry& a, const shortcut_entry& b) {
        return a.keys.size() > b.keys.size();
    });
    return true;
}

config* load_config(const char* path) {
    config* cfg = new config();

    long file_size;
    char* content = read_config_file(path, &file_size);
    if (!content) {
        delete cfg;
        return nullptr;
    }

    if (!parse_config_json(content, *cfg)) {
        free(content);
        delete cfg;
        return nullptr;
    }

    free(content);
    return cfg;
}
