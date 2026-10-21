#ifndef LOG_H
#define LOG_H

#include <stdbool.h>

bool log_open(const char *path);

void log_close(void);

#endif // LOG_H
