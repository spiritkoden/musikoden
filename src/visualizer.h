#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <SDL3/SDL.h>

#include "decoder.h"

typedef struct {
    int    columns;
    float *minv;
    float *maxv;
} WaveOverview;

void overview_build(WaveOverview *o, const DecodedAudio *audio, int columns);
void overview_free(WaveOverview *o);

void overview_draw(SDL_Renderer *r, const WaveOverview *o, const SDL_FRect *rect,
                   double progress);

void scope_draw(SDL_Renderer *r, const float *samples, int n,
                const SDL_FRect *rect);

#endif // VISUALIZER_H
