// strdup() and the dirent/stat APIs are POSIX; expose them under -std=c11.
#define _POSIX_C_SOURCE 200809L

#include "playlist.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "decoder.h"

static bool is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

static void push(Playlist *p, const char *path) {
    char **grown = (char **)realloc(p->items, (size_t)(p->count + 1) * sizeof(char *));
    if (!grown)
        return;
    p->items = grown;
    p->items[p->count] = strdup(path);
    if (p->items[p->count])
        p->count++;
}

static int cmp_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void scan_dir(Playlist *p, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "playlist: cannot open directory: %s\n", dir);
        return;
    }

    int start = p->count;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        if (!is_audio_file(ent->d_name))
            continue;
        char path[4096];
        int n = snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        if (n > 0 && (size_t)n < sizeof path)
            push(p, path);
    }
    closedir(d);

    if (p->count - start > 1)
        qsort(p->items + start, (size_t)(p->count - start), sizeof(char *), cmp_names);
}

void playlist_build(Playlist *out, char **argv, int argc, int start) {
    out->items = NULL;
    out->count = 0;

    for (int i = start; i < argc; ++i) {
        const char *arg = argv[i];
        if (is_dir(arg)) {
            scan_dir(out, arg);
        } else if (is_audio_file(arg)) {
            push(out, arg);
        } else {
            fprintf(stderr, "playlist: skipping (not audio): %s\n", arg);
        }
    }
}

const char *path_basename(const char *path) {
    if (!path)
        return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void playlist_free(Playlist *p) {
    if (!p)
        return;
    for (int i = 0; i < p->count; ++i)
        free(p->items[i]);
    free(p->items);
    p->items = NULL;
    p->count = 0;
}
