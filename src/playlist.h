#ifndef PLAYLIST_H
#define PLAYLIST_H

typedef struct {
    char **items;
    int    count;
} Playlist;

void playlist_build(Playlist *out, char **argv, int argc, int start);
void playlist_free(Playlist *p);

// part of path after the last '/', never NULL
const char *path_basename(const char *path);

#endif // PLAYLIST_H
