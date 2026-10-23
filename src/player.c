#include "player.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "playlist.h"

static uint64_t queue_target_frames(const Player *p) {
    return (uint64_t)p->track.sample_rate / 10u;
}

#define PUSH_CHUNK_FRAMES 4096u

bool player_init(Player *p, char **paths, int count, int index) {
    memset(p, 0, sizeof *p);
    p->paths   = paths;
    p->count   = count;
    p->current = -1;
    p->volume  = 1.0f;

    if (index >= 0)
        return player_open(p, index);
    return true;
}

bool player_open(Player *p, int index) {
    if (index < 0 || index >= p->count)
        return false;

    p->load_id++;

    audio_pause(&p->audio);
    audio_clear(&p->audio);
    audio_destroy(&p->audio);
    decoded_audio_free(&p->track);

    DecodedAudio track;
    if (!decoded_audio_load(p->paths[index], &track)) {
        p->current = -1;
        p->playing = false;
        SDL_Log("player: failed to load %s", p->paths[index]);
        return false;
    }
    p->track   = track;
    p->current = index;

    p->base_frame = 0;
    p->frames_put = 0;
    p->cursor     = 0;
    p->playing    = false;

    if (!audio_init(&p->audio, track.sample_rate, track.channels)) {
        SDL_Log("player: loaded '%s' but audio is unavailable; "
                "playback disabled", p->paths[index]);
        return true;
    }

    audio_set_gain(&p->audio, p->volume);
    p->playing = true;
    audio_resume(&p->audio);
    SDL_Log("player: playing '%s' (%.1f s)", p->paths[index], player_duration(p));
    return true;
}

void player_destroy(Player *p) {
    audio_destroy(&p->audio);
    decoded_audio_free(&p->track);
    memset(p, 0, sizeof *p);
    p->current = -1;
}

void player_play(Player *p) {
    if (!p->track.samples || !p->audio.available)
        return;
    // pressing play at the very end replays the track instead of doing nothing
    if (p->cursor >= p->track.frames && audio_queued_frames(&p->audio) == 0)
        player_seek_seconds(p, 0.0);
    p->playing = true;
    audio_resume(&p->audio);
}

void player_pause(Player *p) {
    p->playing = false;
    audio_pause(&p->audio);
}

void player_toggle(Player *p) {
    if (p->playing)
        player_pause(p);
    else
        player_play(p);
}

void player_tick(Player *p) {
    if (!p->track.samples || !p->playing || !p->audio.available)
        return;

    uint64_t target = queue_target_frames(p);
    while (p->cursor < p->track.frames) {
        if (audio_queued_frames(&p->audio) >= target)
            break;
        uint64_t remaining = p->track.frames - p->cursor;
        uint32_t chunk = (remaining < PUSH_CHUNK_FRAMES)
                             ? (uint32_t)remaining
                             : PUSH_CHUNK_FRAMES;
        const float *src = p->track.samples + p->cursor * p->track.channels;
        if (!audio_push(&p->audio, src, chunk)) {
            p->playing = false;
            return;
        }
        p->cursor     += chunk;
        p->frames_put += chunk;
    }

    if (p->cursor >= p->track.frames && audio_queued_frames(&p->audio) == 0) {
        if (p->loop_track) {
            player_seek_seconds(p, 0.0);
        } else if (p->current + 1 < p->count) {
            player_next(p);
        } else {
            p->playing = false;
        }
    }
}

void player_next(Player *p) {
    if (p->count <= 0)
        return;
    int next = p->current + 1;
    if (next >= p->count) {
        if (!p->loop_track)
            return;
        next = 0;
    }
    player_open(p, next);
}

void player_prev(Player *p) {
    if (p->count <= 0)
        return;
    int prev = p->current - 1;
    if (prev < 0) {
        if (!p->loop_track)
            return;
        prev = p->count - 1;
    }
    player_open(p, prev);
}

static uint64_t clamp_frame(const Player *p, uint64_t f) {
    return f > p->track.frames ? p->track.frames : f;
}

void player_seek_seconds(Player *p, double seconds) {
    if (!p->track.samples)
        return;
    if (seconds < 0.0)
        seconds = 0.0;

    // round to the nearest frame so seeking to the exact duration lands on
    // the end even when the floating-point conversion is a hair short
    double frame_pos = seconds * (double)p->track.sample_rate;
    uint64_t f = frame_pos >= (double)p->track.frames
                     ? p->track.frames
                     : (uint64_t)(frame_pos + 0.5);
    f = clamp_frame(p, f);

    audio_clear(&p->audio);
    p->cursor     = f;
    p->base_frame = f;
    p->frames_put = 0;
}

void player_seek_relative(Player *p, double delta) {
    player_seek_seconds(p, player_position(p) + delta);
}

void player_set_volume(Player *p, float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    p->volume = v;
    audio_set_gain(&p->audio, v);
}

void player_adjust_volume(Player *p, float delta) {
    player_set_volume(p, p->volume + delta);
}

double player_position(const Player *p) {
    if (!p->track.samples || p->track.sample_rate == 0)
        return 0.0;
    uint64_t queued = audio_queued_frames(&p->audio);
    uint64_t played = p->frames_put > queued ? p->frames_put - queued : 0;
    uint64_t pos    = clamp_frame(p, p->base_frame + played);
    return (double)pos / (double)p->track.sample_rate;
}

double player_duration(const Player *p) {
    if (p->track.sample_rate == 0)
        return 0.0;
    return (double)p->track.frames / (double)p->track.sample_rate;
}

const char *player_current_name(const Player *p) {
    if (p->current < 0)
        return "";
    return path_basename(p->paths[p->current]);
}
