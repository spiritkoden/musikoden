#ifndef DECODER_H
#define DECODER_H

#include <stdbool.h>
#include <stdint.h>

// Streaming decoder: a file is opened once and decoded in small chunks, so
// arbitrarily long tracks play with bounded memory instead of being loaded
// into RAM in one shot.

typedef struct Decoder Decoder;

typedef struct {
    uint64_t frames;      // total interleaved PCM frames per channel (0 if unknown)
    uint32_t sample_rate;
    uint32_t channels;
} AudioInfo;

// Opens the file and prepares it for streaming. On success returns a decoder
// (owned by the caller, released with decoder_close()) and fills *info.
// Returns NULL on failure.
Decoder *decoder_open(const char *path, AudioInfo *info);

void decoder_close(Decoder *d);

// Decodes up to `frames` frames of interleaved f32 PCM into `dst`.
// Returns the number of frames written: 0 means end of stream (or an error).
uint32_t decoder_read(Decoder *d, float *dst, uint32_t frames);

// Seeks so the next decoder_read() starts at (or just after) `frame`.
// Returns false if the format cannot seek.
bool decoder_seek(Decoder *d, uint64_t frame);

void format_time(double seconds, char *buf, int bufsize);

bool is_audio_file(const char *path);

#endif // DECODER_H
