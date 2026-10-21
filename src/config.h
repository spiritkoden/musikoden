#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#define CONFIG_PATH_MAX  4096
#define CONFIG_VALUE_MAX 1024

typedef struct {
    char path[CONFIG_PATH_MAX];
    char font[CONFIG_VALUE_MAX];
    char log[CONFIG_VALUE_MAX];
    int  bg;
    int  width;
    int  height;
} Config;

// Call before log_open(): parse warnings go to stderr until then
bool config_load(Config *cfg);

bool config_save_resolution(const Config *cfg, int width, int height);

#endif // CONFIG_H
