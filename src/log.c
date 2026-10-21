// localtime_r() is POSIX; expose it under -std=c11.
#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <stdio.h>
#include <time.h>

#include <SDL3/SDL.h>

static FILE                *g_file    = NULL;
static SDL_LogOutputFunction g_prev    = NULL;
static void                *g_prev_ud  = NULL;

static const char *level_name(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE:  return "VERBOSE";
    case SDL_LOG_PRIORITY_DEBUG:    return "DEBUG";
    case SDL_LOG_PRIORITY_INFO:     return "INFO";
    case SDL_LOG_PRIORITY_WARN:     return "WARN";
    case SDL_LOG_PRIORITY_ERROR:    return "ERROR";
    case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
    default:                        return "LOG";
    }
}

static void log_output(void *userdata, int category, SDL_LogPriority priority,
                       const char *message) {
    (void)userdata;

    if (g_file) {
        char ts[32];
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
        fprintf(g_file, "[%s] %-8s %s\n", ts, level_name(priority), message);
        fflush(g_file);
    }

    if (g_prev)
        g_prev(g_prev_ud, category, priority, message);
}

bool log_open(const char *path) {
    if (g_file)
        return true;
    if (!path)
        return false;

    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    g_file = f;
    SDL_GetLogOutputFunction(&g_prev, &g_prev_ud);
    SDL_SetLogOutputFunction(log_output, NULL);
    return true;
}

void log_close(void) {
    if (!g_file)
        return;

    SDL_SetLogOutputFunction(g_prev, g_prev_ud);
    fflush(g_file);
    fclose(g_file);
    g_file = NULL;
}
