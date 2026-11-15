#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stdint.h>

#include <SDL3/SDL.h>

typedef struct {
    int    columns;
    float *minv;
    float *maxv;
} WaveOverview;

// A background scanner that decodes a track from start to finish and records
// per-bin min/max peaks, so the full waveform overview is available without
// holding the decoded audio in memory. Playback uses its own decoder and is
// unaffected by the scan.
typedef struct WaveScan WaveScan;

// Starts scanning `path` (NULL means "nothing to scan"). `frames`/`channels`
// are the track's totals (frames may be 0 when unknown). The caller owns the
// returned handle and must wavescan_free() it.
WaveScan *wavescan_start(const char *path, uint64_t frames, uint32_t channels);

// Cancels and joins the scan thread, then frees everything.
void wavescan_free(WaveScan *s);

// Builds/refreshes `o` with `columns` columns from whatever has been scanned
// so far. Cheap (a bounded downsample), so it can run every frame. Returns
// scan progress in [0,1].
double wavescan_build(WaveScan *s, WaveOverview *o, int columns);

void overview_free(WaveOverview *o);

void overview_draw(SDL_Renderer *r, const WaveOverview *o, const SDL_FRect *rect,
                   double progress);

void scope_draw(SDL_Renderer *r, const float *samples, int n,
                const SDL_FRect *rect);

#endif // VISUALIZER_H
