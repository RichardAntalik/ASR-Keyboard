#ifndef CONFIG_PARSING_H
#define CONFIG_PARSING_H

#include "extern/json.hpp"
#include "keyboard-sim.h"

config* load_config(const char* path);
config* create_default_config(const char* path);

#endif
