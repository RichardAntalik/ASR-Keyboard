#include "screen-manager.h"
#include <curses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <array>
#include <cstring>
#include <pulse/pulseaudio.h>
#include <algorithm>

std::mutex screen_mutex;

inline constexpr char const* VU_PREFIX = "[";
inline constexpr char const* VU_SUFFIX = "]";
inline constexpr char const* VU_CHARS = ".:=#";
inline constexpr int SHORTCUT_LINES = 5;
inline constexpr char SEP_CHAR = '_';
inline constexpr int OUTPUT_LINES = 8;

static int shortcut_count = 0;
static std::array<std::string, OUTPUT_LINES> output_buf;
static int output_count = 0;

static void output_shift_down(const char* new_line) {
    if (output_count < OUTPUT_LINES) {
        output_buf[output_count] = "";
        output_count++;
    }
    std::rotate(output_buf.rbegin(), output_buf.rbegin() + 1, output_buf.rend());
    output_buf[0] = new_line;
}

static void output_render() {
    int start_line = shortcut_count + 4;
    for (int i = 0; i < output_count; i++) {
        int actual_line = start_line + i;
        if (actual_line >= LINES) break;
        move(actual_line, 0);
        clrtoeol();
        printw("%s", output_buf[i].c_str());
    }
    for (int i = output_count; i < OUTPUT_LINES; i++) {
        int actual_line = start_line + i;
        if (actual_line >= LINES) break;
        move(actual_line, 0);
        clrtoeol();
    }
}

void screen_init() {
    initscr();
    cbreak();
    nocbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    refresh();
}

void screen_cleanup() {
    curs_set(1);
    endwin();
}

static void build_shortcut_line(const shortcut_entry& entry, char* line, int max_len) {
    line[0] = '\0';
    strncat(line, "  Shortcut: ", max_len - strlen(line) - 1);
    strncat(line, entry.keys[0].c_str(), max_len - strlen(line) - 1);

    for (size_t j = 1; j < entry.keys.size(); j++) {
        strncat(line, "+", max_len - strlen(line) - 1);
        strncat(line, entry.keys[j].c_str(), max_len - strlen(line) - 1);
    }

    strncat(line, " -> prompt: ", max_len - strlen(line) - 1);
    strncat(line, entry.prompt.c_str(), max_len - strlen(line) - 1);

    if (!entry.special_keys.empty()) {
        strncat(line, ", special: ", max_len - strlen(line) - 1);
        for (size_t j = 0; j < entry.special_keys.size(); j++) {
            strncat(line, entry.special_keys[j].c_str(), max_len - strlen(line) - 1);
            if (j + 1 < entry.special_keys.size()) {
                strncat(line, "+", max_len - strlen(line) - 1);
            }
        }
    }
}

static void draw_separator(int sep_line) {
    int term_w = getmaxx(stdscr);
    int sep_len = term_w > 250 ? 250 : term_w;

    char sep[256];
    for (int i = 0; i < sep_len; i++) {
        sep[i] = SEP_CHAR;
    }
    sep[sep_len] = '\0';

    move(sep_line, 0);
    printw("%s", sep);

    move(sep_line + 1, 0);
    clrtoeol();
    move(sep_line + 2, 0);
    clrtoeol();
}

void screen_draw_shortcuts(const config* cfg) {
    shortcut_count = static_cast<int>(cfg->entries.size());
    if (shortcut_count > SHORTCUT_LINES) shortcut_count = SHORTCUT_LINES;

    int term_width = getmaxx(stdscr);
    int max_line_width = term_width - 4;

    int line_idx = 0;
    for (const auto& entry : cfg->entries) {
        if (line_idx >= shortcut_count) break;
        char line[512];
        build_shortcut_line(entry, line, sizeof(line));

        if (static_cast<int>(strlen(line)) > max_line_width) {
            line[max_line_width] = '\0';
            strcat(line, "...");
        }

        move(line_idx, 0);
        clrtoeol();
        printw("%s", line);
        line_idx++;
    }

    for (int i = shortcut_count; i < SHORTCUT_LINES; i++) {
        move(i, 0);
        clrtoeol();
    }

    draw_separator(shortcut_count);
    output_render();
    refresh();
}

void screen_draw_vu_meter(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    int term_width = getmaxx(stdscr);
    if (term_width < 15) return;

    int bar_width = term_width - 9;
    if (bar_width < 1) bar_width = 1;

    int filled = static_cast<int>(volume * bar_width);
    if (filled > bar_width) filled = bar_width;

    int vu_line = shortcut_count + 2;
    move(vu_line, 0);

    char buf[1024];
    snprintf(buf, sizeof(buf), "  Mic: [");

    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            buf[strlen(buf)] = VU_CHARS[3];
        } else {
            buf[strlen(buf)] = VU_CHARS[0];
        }
        buf[strlen(buf) + 1] = '\0';
    }

    buf[strlen(buf)] = ']';
    buf[strlen(buf) + 1] = '\0';

    printw("%s", buf);
    move(vu_line + 1, 0);
    clrtoeol();
    refresh();
}

void screen_print(int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    output_shift_down(buf);
    output_render();
    refresh();
}

void screen_refresh() {
    refresh();
}

void screen_handle_resize(const config* cfg) {
    endwin();
    resize_term(0, 0);
    refresh();
    output_count = 0;
    for (auto& line : output_buf) line.clear();
    screen_draw_shortcuts(cfg);
    screen_draw_vu_meter(0.0f);
    refresh();
}
