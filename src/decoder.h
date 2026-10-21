#ifndef DECODER_H
#define DECODER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float    *samples;
    uint64_t  frames;
    uint32_t  sample_rate;
    uint32_t  channels;
} DecodedAudio;

bool decoded_audio_load(const char *path, DecodedAudio *out);

void decoded_audio_free(DecodedAudio *a);

void format_time(double seconds, char *buf, int bufsize);

bool is_audio_file(const char *path);

#endif // DECODER_H
