// player.h — playlist + playback state machine.
//
// Position model: base_frame is the track frame where the current push
// sequence began (reset on load/seek); frames_put counts frames pushed since
// then. Frames the device has actually consumed = frames_put - queued, so the
// audible position = base_frame + frames_put - queued.

#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>

#include "audio.h"
#include "decoder.h"

typedef struct {
    char **paths;   // borrowed array of file paths (owned by caller)
    int    count;
    int    current; // index of loaded track, or -1

    // bumped on every player_open() attempt; lets the UI tell two loads apart
    // even when the allocator reuses the previous sample buffer's address
    uint64_t load_id;

    DecodedAudio track;
    AudioOut     audio;

    uint64_t base_frame;
    uint64_t frames_put;
    uint64_t cursor;

    bool  playing;
    bool  loop_track;
    float volume;
} Player;

// paths is borrowed; it must outlive the player
bool player_init(Player *p, char **paths, int count, int index);
void player_destroy(Player *p);

bool player_open(Player *p, int index);

void player_play(Player *p);
void player_pause(Player *p);
void player_toggle(Player *p);
void player_tick(Player *p);

void player_seek_seconds(Player *p, double seconds);
void player_seek_relative(Player *p, double delta);
void player_set_volume(Player *p, float v);
void player_adjust_volume(Player *p, float delta);

void player_next(Player *p);
void player_prev(Player *p);

double      player_position(const Player *p);
double      player_duration(const Player *p);
const char *player_current_name(const Player *p);

#endif // PLAYER_H
