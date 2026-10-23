// audio.h — thin wrapper over one SDL3 audio output stream.
//
// The main loop is the only writer of both the stream and the scope tap,
// so no locking is needed.

#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#define AUDIO_SCOPE_CAP 8192u

typedef struct {
    SDL_AudioStream *stream;
    uint32_t channels;
    uint32_t sample_rate;
    bool     available;

    float    scope[AUDIO_SCOPE_CAP];
    uint64_t scope_written;
} AudioOut;

bool audio_init(AudioOut *a, uint32_t sample_rate, uint32_t channels);
void audio_destroy(AudioOut *a);

void audio_resume(AudioOut *a);
void audio_pause(AudioOut *a);
void audio_clear(AudioOut *a);
void audio_set_gain(AudioOut *a, float gain);

uint64_t audio_queued_frames(const AudioOut *a);

bool audio_push(AudioOut *a, const float *frames, uint32_t n);

void audio_scope_read(const AudioOut *a, float *dst, int n);

#endif // AUDIO_H
