#include "audio.h"

#include <string.h>

bool audio_init(AudioOut *a, uint32_t sample_rate, uint32_t channels) {
    memset(a, 0, sizeof *a);
    a->sample_rate = sample_rate;
    a->channels    = channels;

    if (channels == 0 || sample_rate == 0)
        return false;

    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_F32;
    spec.channels = (int)channels;
    spec.freq     = (int)sample_rate;

    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &spec, NULL, NULL);
    if (!a->stream) {
        SDL_Log("audio: could not open playback stream: %s", SDL_GetError());
        a->available = false;
        return false;
    }

    a->available = true;
    SDL_ResumeAudioStreamDevice(a->stream);
    return true;
}

void audio_destroy(AudioOut *a) {
    if (a->stream)
        SDL_DestroyAudioStream(a->stream);
    memset(a, 0, sizeof *a);
}

void audio_resume(AudioOut *a) {
    if (a->stream)
        SDL_ResumeAudioStreamDevice(a->stream);
}

void audio_pause(AudioOut *a) {
    if (a->stream)
        SDL_PauseAudioStreamDevice(a->stream);
}

void audio_clear(AudioOut *a) {
    if (a->stream)
        SDL_ClearAudioStream(a->stream);
}

void audio_set_gain(AudioOut *a, float gain) {
    if (a->stream)
        SDL_SetAudioStreamGain(a->stream, gain);
}

uint64_t audio_queued_frames(const AudioOut *a) {
    if (!a->stream)
        return 0;
    int bytes = SDL_GetAudioStreamQueued(a->stream);
    if (bytes <= 0)
        return 0;
    uint64_t bytes_per_frame = (uint64_t)a->channels * sizeof(float);
    return (uint64_t)bytes / bytes_per_frame;
}

bool audio_push(AudioOut *a, const float *frames, uint32_t n) {
    if (!a->stream || !a->available)
        return false;
    if (n == 0)
        return true;

    int bytes = (int)((size_t)n * a->channels * sizeof(float));
    if (!SDL_PutAudioStreamData(a->stream, frames, (int)bytes)) {
        SDL_Log("audio: could not queue playback data: %s", SDL_GetError());
        a->available = false;
        return false;
    }

    uint32_t ch = a->channels;
    for (uint32_t i = 0; i < n; ++i) {
        float m = 0.0f;
        for (uint32_t c = 0; c < ch; ++c)
            m += frames[(size_t)i * ch + c];
        m /= (float)ch;
        a->scope[a->scope_written % AUDIO_SCOPE_CAP] = m;
        a->scope_written++;
    }
    return true;
}

void audio_scope_read(const AudioOut *a, float *dst, int n) {
    if (n <= 0)
        return;
    if ((uint32_t)n > AUDIO_SCOPE_CAP)
        n = (int)AUDIO_SCOPE_CAP;

    uint64_t total = a->scope_written;
    for (int i = 0; i < n; ++i) {
        int64_t idx = (int64_t)total - (int64_t)n + (int64_t)i;
        if (idx < 0)
            dst[i] = 0.0f;
        else
            dst[i] = a->scope[(uint64_t)idx % AUDIO_SCOPE_CAP];
    }
}
