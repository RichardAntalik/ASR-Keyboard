#include "screen-manager.h"
#include <curses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <cstring>
#include <signal.h>
#include <pulse/pulseaudio.h>

#define VU_PREFIX "["
#define VU_SUFFIX "]"
#define VU_CHARS ".:=#"
#define SHORTCUT_LINES 5
#define SEP_CHAR '_'
#define OUTPUT_LINES 8

static int shortcut_count = 0;
static char output_buf[OUTPUT_LINES][1024];
static int output_count = 0;

static void output_shift_down(const char* new_line) {
    for (int i = OUTPUT_LINES - 1; i > 0; i--) {
        strcpy(output_buf[i], output_buf[i - 1]);
    }
    strncpy(output_buf[0], new_line, sizeof(output_buf[0]) - 1);
    output_buf[0][sizeof(output_buf[0]) - 1] = '\0';
    if (output_count < OUTPUT_LINES) output_count++;
}

static void output_render() {
    int start_line = shortcut_count + 4;
    for (int i = 0; i < output_count; i++) {
        int actual_line = start_line + i;
        if (actual_line >= LINES) break;
        move(actual_line, 0);
        clrtoeol();
        printw("%s", output_buf[i]);
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

void screen_draw_shortcuts(const config* cfg) {
    shortcut_count = cfg->entry_count;
    if (shortcut_count > SHORTCUT_LINES) shortcut_count = SHORTCUT_LINES;

    int term_width = getmaxx(stdscr);
    int max_line_width = term_width - 4;

    for (int i = 0; i < shortcut_count; i++) {
        char line[512] = "";
        strncat(line, "  Shortcut: ", sizeof(line) - strlen(line) - 1);
        strncat(line, cfg->entries[i].keys[0], sizeof(line) - strlen(line) - 1);
        for (int j = 1; j < cfg->entries[i].key_count; j++) {
            strncat(line, "+", sizeof(line) - strlen(line) - 1);
            strncat(line, cfg->entries[i].keys[j], sizeof(line) - strlen(line) - 1);
        }
        strncat(line, " -> prompt: ", sizeof(line) - strlen(line) - 1);
        strncat(line, cfg->entries[i].prompt, sizeof(line) - strlen(line) - 1);
        if (cfg->entries[i].special_key_count > 0) {
            strncat(line, ", special: ", sizeof(line) - strlen(line) - 1);
            for (int j = 0; j < cfg->entries[i].special_key_count; j++) {
                strncat(line, cfg->entries[i].special_keys[j], sizeof(line) - strlen(line) - 1);
                if (j + 1 < cfg->entries[i].special_key_count) {
                    strncat(line, "+", sizeof(line) - strlen(line) - 1);
                }
            }
        }

        if ((int)strlen(line) > max_line_width) {
            line[max_line_width] = '\0';
            strcat(line, "...");
        }

        move(i, 0);
        clrtoeol();
        printw("%s", line);
    }

    for (int i = shortcut_count; i < SHORTCUT_LINES; i++) {
        move(i, 0);
        clrtoeol();
    }

    int sep_line = shortcut_count;
    char sep[256];
    int term_w = getmaxx(stdscr);
    int sep_len = term_w > 250 ? 250 : term_w;
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

    int filled = (int)(volume * bar_width);
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
    memset(output_buf, 0, sizeof(output_buf));
    screen_draw_shortcuts(cfg);
    screen_draw_vu_meter(0.0f);
    refresh();
}
