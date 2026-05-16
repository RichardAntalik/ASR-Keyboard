#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include <curses.h>
#include <pthread.h>
#include "keyboard-sim.h"

extern pthread_mutex_t screen_mutex;

void screen_init();
void screen_cleanup();
void screen_draw_shortcuts(const config* cfg);
void screen_draw_vu_meter(float volume);
void screen_print(int line, const char* fmt, ...);
void screen_refresh();
void screen_handle_resize(const config* cfg);

#endif
