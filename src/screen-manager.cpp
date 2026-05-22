#include "screen-manager.h"
#include <curses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <deque>
#include <cstring>
#include <pulse/pulseaudio.h>
#include <algorithm>
#include <cctype>

std::mutex screen_mutex;

inline constexpr char const* VU_PREFIX = "[";
inline constexpr char const* VU_SUFFIX = "]";
inline constexpr char const* VU_CHARS = ".:=#";
inline constexpr int SHORTCUT_LINES = 5;
inline constexpr char SEP_CHAR = '_';

static int shortcut_count = 0;
static std::deque<std::string> output_buf;
static std::deque<std::string> server_buf;

static int left_width = 0;
static int right_width = 0;
static int separator_x = 0;
static int right_start_x = 0;
static int output_start_line = 0;

static void calc_layout() {
    int term_width = getmaxx(stdscr);
    left_width = term_width / 2;
    right_width = term_width - left_width - 1;
    separator_x = left_width;
    right_start_x = left_width + 1;
    output_start_line = shortcut_count + 4;
}

static void clear_left_line(int y) {
    for (int x = 0; x < left_width; x++) {
        mvaddch(y, x, ' ');
    }
}

static void clear_right_line(int y) {
    int term_width = getmaxx(stdscr);
    for (int x = right_start_x; x < term_width; x++) {
        mvaddch(y, x, ' ');
    }
}

static void draw_vertical_separator() {
    for (int y = output_start_line; y < LINES; y++) {
        mvaddch(y, separator_x, '|');
    }
}

static void output_render();
void screen_draw_server_output();

static void output_shift_down(const char* new_line) {
    output_buf.push_front(new_line);
    while ((int)output_buf.size() > LINES) {
        output_buf.pop_back();
    }
}

static void output_render() {
    int total_lines = LINES - output_start_line;
    if (total_lines < 0) total_lines = 0;
    int render_count = output_buf.size();
    if (render_count > total_lines) render_count = total_lines;
    auto it = output_buf.begin();
    for (int i = 0; i < render_count; i++) {
        clear_left_line(output_start_line + i);
        move(output_start_line + i, 0);
        int line_len = static_cast<int>(it->length());
        if (line_len > left_width) {
            std::string truncated = it->substr(0, left_width);
            printw("%s", truncated.c_str());
        } else {
            printw("%s", it->c_str());
        }
        it++;
    }
    for (int i = render_count; i < total_lines; i++) {
        clear_left_line(output_start_line + i);
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

static void capitalize_first(char* s) {
    if (s && s[0]) {
        s[0] = toupper(static_cast<unsigned char>(s[0]));
    }
}

static void build_shortcut_line(const shortcut_entry& entry, char* line, int max_len) {
    line[0] = '\0';
    strncat(line, "  ", max_len - strlen(line) - 1);
    char key_copy[64];
    strncpy(key_copy, entry.keys[0].c_str(), sizeof(key_copy) - 1);
    key_copy[sizeof(key_copy) - 1] = '\0';
    capitalize_first(key_copy);
    strncat(line, key_copy, max_len - strlen(line) - 1);

    for (size_t j = 1; j < entry.keys.size(); j++) {
        strncat(line, "+", max_len - strlen(line) - 1);
        strncpy(key_copy, entry.keys[j].c_str(), sizeof(key_copy) - 1);
        key_copy[sizeof(key_copy) - 1] = '\0';
        capitalize_first(key_copy);
        strncat(line, key_copy, max_len - strlen(line) - 1);
    }

    strncat(line, " - ", max_len - strlen(line) - 1);
    strncat(line, entry.prompt.c_str(), max_len - strlen(line) - 1);

    if (!entry.special_keys.empty()) {
        strncat(line, " - special keys: ", max_len - strlen(line) - 1);
        for (size_t j = 0; j < entry.special_keys.size(); j++) {
            strncpy(key_copy, entry.special_keys[j].c_str(), sizeof(key_copy) - 1);
            key_copy[sizeof(key_copy) - 1] = '\0';
            capitalize_first(key_copy);
            strncat(line, key_copy, max_len - strlen(line) - 1);
            if (j + 1 < entry.special_keys.size()) {
                strncat(line, ", ", max_len - strlen(line) - 1);
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

    calc_layout();
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
    screen_draw_server_output();
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
        int len = strlen(buf);
        if (i < filled) {
            buf[len] = VU_CHARS[3];
        } else {
            buf[len] = VU_CHARS[0];
        }
        buf[len + 1] = '\0';
    }

    buf[strlen(buf)] = ']';
    buf[strlen(buf) + 1] = '\0';

    printw("%s", buf);
    for (int x = strlen(buf); x < term_width; x++) {
        mvaddch(vu_line + 1, x, ' ');
    }
    refresh();
}

void screen_print(int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    output_shift_down(buf);
    calc_layout();
    output_render();
    screen_draw_server_output();
    refresh();
}

void screen_debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(screen_mutex);
    output_shift_down(buf);
    calc_layout();
    output_render();
    screen_draw_server_output();
    refresh();
}

void screen_refresh() {
    refresh();
}

void screen_handle_resize(const config* cfg) {
    endwin();
    resize_term(0, 0);
    refresh();
    calc_layout();
    draw_vertical_separator();
    screen_draw_shortcuts(cfg);
    screen_draw_vu_meter(0.0f);
    screen_draw_server_output();
    refresh();
}

void screen_draw_server_output() {
    int total_lines = LINES - output_start_line;
    if (total_lines < 0) total_lines = 0;
    int render_count = server_buf.size();
    if (render_count > total_lines) render_count = total_lines;
    auto it = server_buf.begin();
    for (int i = 0; i < render_count; i++) {
        clear_right_line(output_start_line + i);
        move(output_start_line + i, right_start_x);
        int line_len = static_cast<int>(it->length());
        if (line_len > right_width) {
            std::string truncated = it->substr(0, right_width);
            printw("%s", truncated.c_str());
        } else {
            printw("%s", it->c_str());
        }
        it++;
    }
    for (int i = render_count; i < total_lines; i++) {
        clear_right_line(output_start_line + i);
    }
}

void screen_push_server_output(const char* line) {
    std::lock_guard<std::mutex> lock(screen_mutex);
    server_buf.push_front(line);
    while ((int)server_buf.size() > LINES) {
        server_buf.pop_back();
    }
    calc_layout();
    draw_vertical_separator();
    output_render();
    screen_draw_server_output();
    refresh();
}
