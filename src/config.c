// mkdir() is POSIX; expose it under -std=c11.
#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL3/SDL.h>

#define CONFIG_DIR  "musikoden"
#define CONFIG_FILE "musikoden.conf"
#define CONFIG_LOG_FILE "musikoden.log"
#define CONFIG_LINE_MAX 2048
#define CONFIG_MAX_BYTES 65536

#define CONFIG_MIN_DIM 320
#define CONFIG_MAX_DIM 7680

#define CONFIG_DEFAULT_BG     0x101016
#define CONFIG_DEFAULT_WIDTH  800
#define CONFIG_DEFAULT_HEIGHT 480

static const char FILE_HEADER[] =
    "# musikoden configuration — 'key = value'; a line starting with '#' is a comment.\n"
    "# font:       path to a .ttf file; empty = probe system fonts (--font).\n"
    "# bg:         canvas background as a hex color, e.g. #101016.\n"
    "# resolution: initial window size as WIDTHxHEIGHT, e.g. 800x480.\n"
    "# The log is always musikoden.log in this directory; use --log FILE to\n"
    "# point it elsewhere.\n"
    "\n";
static const char FILE_DEFAULTS[] =
    "font =\n"
    "bg = #101016\n";

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && is_space(s[len - 1]))
        s[--len] = '\0';
    if (s[0] && is_space(s[0])) {
        char *p = s;
        while (*p && is_space(*p))
            p++;
        memmove(s, p, strlen(p) + 1);
    }
}

static void copy_value(char *dst, size_t size, const char *src, const char *key) {
    size_t n = strlen(src);
    if (n >= size) {
        SDL_Log("config: value for '%s' is too long; truncated", key);
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool join(char *dst, size_t size, const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la + lb + 1 > size)
        return false;
    memcpy(dst, a, la);
    memcpy(dst + la, b, lb + 1);
    return true;
}

// a relative $XDG_CONFIG_HOME is ignored: the XDG spec requires absolute
static bool resolve_dir(char *dir, size_t size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *src = (xdg && xdg[0] == '/') ? xdg : NULL;
    char parent[CONFIG_PATH_MAX];

    if (src) {
        copy_value(parent, sizeof parent, src, "XDG_CONFIG_HOME");
    } else {
        const char *home = getenv("HOME");
        if (!home || !home[0])
            return false;
        if (!join(parent, sizeof parent, home, "/.config"))
            return false;
    }

    if (mkdir(parent, (mode_t)0755) != 0 && errno != EEXIST) {
        SDL_Log("config: cannot create '%s': %s", parent, strerror(errno));
        return false;
    }
    if (!join(dir, size, parent, "/" CONFIG_DIR))
        return false;
    if (mkdir(dir, (mode_t)0755) != 0 && errno != EEXIST) {
        SDL_Log("config: cannot create '%s': %s", dir, strerror(errno));
        return false;
    }
    return true;
}

static void parse_file(Config *cfg, FILE *f) {
    char line[CONFIG_LINE_MAX];
    int lineno = 0;

    while (fgets(line, sizeof line, f)) {
        lineno++;

        // '#' starts a comment only at line start, so 'bg = #101016' parses
        trim(line);
        if (!line[0] || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            SDL_Log("config: line %d: expected 'key = value'", lineno);
            continue;
        }
        *eq = '\0';
        char *key = line, *value = eq + 1;
        trim(key);
        trim(value);

        if (strcmp(key, "font") == 0) {
            copy_value(cfg->font, sizeof cfg->font, value, key);
        } else if (strcmp(key, "log") == 0) {
            // legacy key, accepted silently (the log always lives beside
            // this file now); an unknown key would warn on every start
        } else if (strcmp(key, "bg") == 0) {
            const char *hex = (value[0] == '#') ? value + 1 : value;
            char *end = NULL;
            long v = strtol(hex, &end, 16);
            size_t digits = (size_t)(end - hex);
            if (digits != 6 || *end != '\0' || v < 0 || v > 0xFFFFFF) {
                SDL_Log("config: line %d: bad bg '%s' (want #RRGGBB); "
                        "keeping #%06x", lineno, value, (unsigned)cfg->bg);
            } else {
                cfg->bg = (int)v;
            }
        } else if (strcmp(key, "resolution") == 0) {
            int w = 0, h = 0;
            char extra;
            if (sscanf(value, "%dx%d%c", &w, &h, &extra) != 2) {
                SDL_Log("config: line %d: bad resolution '%s' "
                        "(want WIDTHxHEIGHT); keeping %dx%d",
                        lineno, value, cfg->width, cfg->height);
            } else {
                if (w < CONFIG_MIN_DIM) w = CONFIG_MIN_DIM;
                if (w > CONFIG_MAX_DIM) w = CONFIG_MAX_DIM;
                if (h < CONFIG_MIN_DIM) h = CONFIG_MIN_DIM;
                if (h > CONFIG_MAX_DIM) h = CONFIG_MAX_DIM;
                cfg->width = w;
                cfg->height = h;
            }
        } else {
            SDL_Log("config: line %d: unknown key '%s'", lineno, key);
        }
    }
}

static bool create_default_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        SDL_Log("config: cannot create '%s': %s", path, strerror(errno));
        return false;
    }
    bool ok = fputs(FILE_HEADER, f) != EOF &&
              fputs(FILE_DEFAULTS, f) != EOF &&
              fprintf(f, "resolution = %dx%d\n",
                      CONFIG_DEFAULT_WIDTH, CONFIG_DEFAULT_HEIGHT) >= 0;
    if (fclose(f) != 0)
        ok = false;
    if (!ok) {
        SDL_Log("config: cannot write '%s': %s", path, strerror(errno));
        remove(path);
        return false;
    }
    SDL_Log("config: created '%s' with defaults", path);
    return true;
}

bool config_load(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->bg     = CONFIG_DEFAULT_BG;
    cfg->width  = CONFIG_DEFAULT_WIDTH;
    cfg->height = CONFIG_DEFAULT_HEIGHT;

    char dir[CONFIG_PATH_MAX];
    if (!resolve_dir(dir, sizeof dir) ||
        !join(cfg->path, sizeof cfg->path, dir, "/" CONFIG_FILE)) {
        cfg->path[0] = '\0';
        return false;
    }

    FILE *f = fopen(cfg->path, "r");
    if (f) {
        parse_file(cfg, f);
        fclose(f);
    } else if (errno != ENOENT) {
        // an existing but unreadable file must not be truncated by
        // create_default_file()
        SDL_Log("config: cannot read '%s': %s", cfg->path, strerror(errno));
        cfg->path[0] = '\0';
        return false;
    } else if (!create_default_file(cfg->path)) {
        cfg->path[0] = '\0';
        return false;
    }

    if (!join(cfg->log, sizeof cfg->log, dir, "/" CONFIG_LOG_FILE))
        SDL_Log("config: default log path too long; falling back to the cwd");
    return true;
}

static bool append_bytes(char *dst, size_t cap, size_t *used,
                         const char *s, size_t n) {
    if (*used + n + 1 > cap)
        return false;
    memcpy(dst + *used, s, n);
    *used += n;
    dst[*used] = '\0';
    return true;
}

bool config_save_resolution(const Config *cfg, int width, int height) {
    if (!cfg->path[0]) {
        SDL_Log("config: persistence disabled; not saving resolution");
        return false;
    }
    if (width < CONFIG_MIN_DIM)  width = CONFIG_MIN_DIM;
    if (width > CONFIG_MAX_DIM)  width = CONFIG_MAX_DIM;
    if (height < CONFIG_MIN_DIM) height = CONFIG_MIN_DIM;
    if (height > CONFIG_MAX_DIM) height = CONFIG_MAX_DIM;

    char source[CONFIG_MAX_BYTES];
    size_t src_len = 0;
    bool file_exists = true;
    FILE *f = fopen(cfg->path, "r");
    if (f) {
        src_len = fread(source, 1, sizeof source - 1, f);
        if (ferror(f)) {
            SDL_Log("config: cannot read '%s': %s", cfg->path, strerror(errno));
            fclose(f);
            return false;
        }
        fclose(f);
        source[src_len] = '\0';
    } else if (errno == ENOENT) {
        file_exists = false;
    } else {
        SDL_Log("config: cannot read '%s': %s", cfg->path, strerror(errno));
        return false;
    }

    char updated[CONFIG_MAX_BYTES + 128];
    size_t used = 0;
    updated[0] = '\0';
    bool ok = true;

    if (file_exists) {
        bool replaced = false;
        const char *p = source;
        while (ok && *p) {
            const char *nl = strchr(p, '\n');
            size_t line_len = nl ? (size_t)(nl - p) + 1 : strlen(p);

            char line[CONFIG_LINE_MAX];
            size_t copy_n = (line_len < CONFIG_LINE_MAX - 1)
                          ? line_len : CONFIG_LINE_MAX - 1;
            memcpy(line, p, copy_n);
            line[copy_n] = '\0';
            trim(line);

            bool is_resolution = false;
            if (!replaced && line[0] && line[0] != '#') {
                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    char *key = line;
                    trim(key);
                    is_resolution = (strcmp(key, "resolution") == 0);
                }
            }

            if (is_resolution) {
                char repl[64];
                int m = SDL_snprintf(repl, sizeof repl,
                                     "resolution = %dx%d\n", width, height);
                ok = m > 0 && append_bytes(updated, sizeof updated, &used,
                                           repl, (size_t)m);
                replaced = true;
            } else {
                ok = append_bytes(updated, sizeof updated, &used, p, line_len);
            }
            p += line_len;
        }
        if (!ok) {
            SDL_Log("config: '%s' too large to update; not saving", cfg->path);
            return false;
        }
        if (!replaced) {
            if (used > 0 && updated[used - 1] != '\n')
                ok = append_bytes(updated, sizeof updated, &used, "\n", 1);
            char repl[64];
            int m = SDL_snprintf(repl, sizeof repl,
                                 "resolution = %dx%d\n", width, height);
            ok = ok && m > 0 &&
                 append_bytes(updated, sizeof updated, &used, repl, (size_t)m);
        }
    } else {
        char repl[64];
        int m = SDL_snprintf(repl, sizeof repl,
                             "resolution = %dx%d\n", width, height);
        ok = append_bytes(updated, sizeof updated, &used,
                          FILE_HEADER, sizeof FILE_HEADER - 1) &&
             append_bytes(updated, sizeof updated, &used,
                          FILE_DEFAULTS, sizeof FILE_DEFAULTS - 1) &&
             m > 0 &&
             append_bytes(updated, sizeof updated, &used, repl, (size_t)m);
    }
    if (!ok) {
        SDL_Log("config: could not compose new content for '%s'", cfg->path);
        return false;
    }

    // atomic replace: sibling .tmp, then rename over the target
    char tmp[CONFIG_PATH_MAX];
    if (!join(tmp, sizeof tmp, cfg->path, ".tmp")) {
        SDL_Log("config: path too long, not saving '%s'", cfg->path);
        return false;
    }
    FILE *out = fopen(tmp, "w");
    if (!out) {
        SDL_Log("config: cannot write '%s': %s", tmp, strerror(errno));
        return false;
    }
    bool wrote = fputs(updated, out) != EOF;
    if (fclose(out) != 0)
        wrote = false;
    if (!wrote) {
        SDL_Log("config: error writing '%s': %s", tmp, strerror(errno));
        remove(tmp);
        return false;
    }
    if (rename(tmp, cfg->path) != 0) {
        SDL_Log("config: cannot replace '%s': %s", cfg->path, strerror(errno));
        remove(tmp);
        return false;
    }
    SDL_Log("config: saved resolution %dx%d to '%s'", width, height, cfg->path);
    return true;
}
