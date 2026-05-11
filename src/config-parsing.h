#ifndef CONFIG_PARSING_H
#define CONFIG_PARSING_H

#include "json.hpp"
#include "keyboard-sim.h"

config* load_config(const char* path);
void free_config(config* cfg);

#endif
