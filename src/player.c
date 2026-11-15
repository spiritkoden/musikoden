#include "player.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "playlist.h"

#define PUSH_CHUNK_FRAMES 4096u

static uint64_t queue_target_frames(const Player *p) {
    if (p->info.sample_rate == 0)
        return PUSH_CHUNK_FRAMES;
    return (uint64_t)p->info.sample_rate / 10u;
}

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

// (re)size the scratch decode buffer to hold at least `channels` channels of
// PUSH_CHUNK_FRAMES frames.
static bool ensure_scratch(Player *p, uint32_t channels) {
    uint32_t cap = PUSH_CHUNK_FRAMES * (channels ? channels : 1u);
    if (p->scratch && p->scratch_frames >= cap)
        return true;
    float *grown = (float *)realloc(p->scratch, (size_t)cap * sizeof(float));
    if (!grown)
        return false;
    p->scratch        = grown;
    p->scratch_frames = cap;
    return true;
}

bool player_open(Player *p, int index) {
    if (index < 0 || index >= p->count)
        return false;

    p->load_id++;

    audio_pause(&p->audio);
    audio_clear(&p->audio);
    audio_destroy(&p->audio);
    decoder_close(p->dec);
    p->dec = NULL;
    memset(&p->info, 0, sizeof p->info);

    AudioInfo info;
    Decoder *dec = decoder_open(p->paths[index], &info);
    if (!dec) {
        p->current = -1;
        p->playing = false;
        SDL_Log("player: failed to load %s", p->paths[index]);
        return false;
    }
    p->dec     = dec;
    p->info    = info;
    p->current = index;

    if (!ensure_scratch(p, info.channels)) {
        decoder_close(p->dec);
        p->dec = NULL;
        p->current = -1;
        SDL_Log("player: out of memory loading %s", p->paths[index]);
        return false;
    }

    p->base_frame = 0;
    p->frames_put = 0;
    p->eof        = false;
    p->playing    = false;

    if (!audio_init(&p->audio, info.sample_rate, info.channels)) {
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
    decoder_close(p->dec);
    free(p->scratch);
    memset(p, 0, sizeof *p);
    p->current = -1;
}

void player_play(Player *p) {
    if (!p->dec || !p->audio.available)
        return;
    // pressing play at the very end replays the track instead of doing nothing
    if (p->eof && audio_queued_frames(&p->audio) == 0)
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
    if (!p->dec || !p->playing || !p->audio.available)
        return;

    uint64_t target = queue_target_frames(p);
    while (!p->eof) {
        if (audio_queued_frames(&p->audio) >= target)
            break;
        uint32_t got = decoder_read(p->dec, p->scratch, PUSH_CHUNK_FRAMES);
        if (got == 0) {
            p->eof = true;
            break;
        }
        if (!audio_push(&p->audio, p->scratch, got)) {
            p->playing = false;
            return;
        }
        p->frames_put += got;
    }

    if (p->eof && audio_queued_frames(&p->audio) == 0) {
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
    if (p->info.frames != 0 && f > p->info.frames)
        return p->info.frames;
    return f;
}

void player_seek_seconds(Player *p, double seconds) {
    if (!p->dec)
        return;
    if (seconds < 0.0)
        seconds = 0.0;

    // round to the nearest frame so seeking to the exact duration lands on
    // the end even when the floating-point conversion is a hair short
    double frame_pos = seconds * (double)p->info.sample_rate;
    uint64_t f = frame_pos >= (double)p->info.frames && p->info.frames != 0
                     ? p->info.frames
                     : (uint64_t)(frame_pos + 0.5);
    f = clamp_frame(p, f);

    audio_clear(&p->audio);
    if (!decoder_seek(p->dec, f))
        SDL_Log("player: seek failed for %s", player_current_name(p));
    p->base_frame = f;
    p->frames_put = 0;
    p->eof        = false;
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
    if (!p->dec || p->info.sample_rate == 0)
        return 0.0;
    uint64_t queued = audio_queued_frames(&p->audio);
    uint64_t played = p->frames_put > queued ? p->frames_put - queued : 0;
    uint64_t pos    = clamp_frame(p, p->base_frame + played);
    return (double)pos / (double)p->info.sample_rate;
}

double player_duration(const Player *p) {
    if (p->info.sample_rate == 0)
        return 0.0;
    return (double)p->info.frames / (double)p->info.sample_rate;
}

const char *player_current_name(const Player *p) {
    if (p->current < 0)
        return "";
    return path_basename(p->paths[p->current]);
}
