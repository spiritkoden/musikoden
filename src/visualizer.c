#include "visualizer.h"

#include <stdlib.h>

void overview_free(WaveOverview *o) {
    if (!o) return;
    free(o->minv);
    free(o->maxv);
    o->minv = o->maxv = NULL;
    o->columns = 0;
}

void overview_build(WaveOverview *o, const DecodedAudio *audio, int columns) {
    overview_free(o);
    if (columns < 1)
        columns = 1;
    o->columns = columns;
    o->minv = (float *)malloc((size_t)columns * sizeof(float));
    o->maxv = (float *)malloc((size_t)columns * sizeof(float));
    if (!o->minv || !o->maxv) {
        overview_free(o);
        return;
    }

    if (!audio->samples || audio->frames == 0) {
        for (int i = 0; i < columns; ++i)
            o->minv[i] = o->maxv[i] = 0.0f;
        return;
    }

    const uint32_t ch = audio->channels;
    const uint64_t frames = audio->frames;

    for (int i = 0; i < columns; ++i) {
        uint64_t s = (uint64_t)((double)i * (double)frames / (double)columns);
        uint64_t e = (uint64_t)((double)(i + 1) * (double)frames / (double)columns);
        if (e <= s) e = s + 1;
        if (e > frames) e = frames;

        float mn = 0.0f, mx = 0.0f;
        for (uint64_t f = s; f < e; ++f) {
            float m = 0.0f;
            for (uint32_t c = 0; c < ch; ++c)
                m += audio->samples[(size_t)f * ch + c];
            m /= (float)ch;
            if (m < mn) mn = m;
            if (m > mx) mx = m;
        }
        o->minv[i] = mn;
        o->maxv[i] = mx;
    }
}

void overview_draw(SDL_Renderer *r, const WaveOverview *o, const SDL_FRect *rect,
                   double progress) {
    if (!o->minv || o->columns < 1 || rect->w <= 0.0f)
        return;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;

    const float center = rect->y + rect->h * 0.5f;
    const float half   = rect->h * 0.5f - 2.0f;
    const float colw   = rect->w / (float)o->columns;

    // one symmetric line per column, its thickness following the peak
    // amplitude: thin where quiet, fat where loud
    for (int i = 0; i < o->columns; ++i) {
        float mn = o->minv[i] < 0.0f ? -o->minv[i] : o->minv[i];
        float mx = o->maxv[i] < 0.0f ? -o->maxv[i] : o->maxv[i];
        float amp = mn > mx ? mn : mx;

        float h = amp * 2.0f * half;
        if (h < 1.0f) h = 1.0f;

        SDL_FRect bar = {
            rect->x + (float)i * colw,
            center - h * 0.5f,
            colw < 1.0f ? 1.0f : colw,
            h,
        };

        double frac = ((double)i + 0.5) / (double)o->columns;
        if (frac <= progress)
            SDL_SetRenderDrawColor(r, 0, 220, 180, 255);
        else
            SDL_SetRenderDrawColor(r, 70, 78, 92, 255);
        SDL_RenderFillRect(r, &bar);
    }

    float px = rect->x + (float)(progress * (double)rect->w);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 230);
    SDL_FRect ph = { px - 1.0f, rect->y, 2.0f, rect->h };
    SDL_RenderFillRect(r, &ph);
}

void scope_draw(SDL_Renderer *r, const float *samples, int n,
                const SDL_FRect *rect) {
    if (!samples || n <= 0 || rect->w <= 0.0f || rect->h <= 0.0f)
        return;

    const float center = rect->y + rect->h * 0.5f;
    float half = rect->h * 0.5f - 4.0f;
    if (half < 1.0f) half = 1.0f;
    const int cols = (int)rect->w;

    // graticule and zero line first, so the trace stays visible on top
    SDL_SetRenderDrawColor(r, 38, 44, 54, 255);
    for (int k = 1; k <= 3; ++k) {
        float y = rect->y + rect->h * ((float)k * 0.25f);
        SDL_RenderLine(r, rect->x, y, rect->x + rect->w, y);
    }
    for (int j = 1; j <= 7; ++j) {
        float x = rect->x + rect->w * ((float)j / 8.0f);
        SDL_RenderLine(r, x, rect->y, x, rect->y + rect->h);
    }
    SDL_SetRenderDrawColor(r, 60, 70, 82, 255);
    SDL_RenderLine(r, rect->x, center, rect->x + rect->w, center);

    // the actual samples, linearly resampled to one point per column
    if (cols > 0 && n > 1) {
        SDL_SetRenderDrawColor(r, 200, 255, 240, 255);
        const float colw = rect->w / (float)cols;
        const float step = (float)(n - 1) / (float)cols;
        float prev_x = rect->x, prev_y = center;
        for (int i = 0; i <= cols; ++i) {
            float pos = (float)i * step;
            int i0 = (int)pos;
            if (i0 > n - 1)
                i0 = n - 1;
            int i1 = i0 + 1 < n ? i0 + 1 : i0;
            float frac = pos - (float)i0;

            float v = samples[i0] + (samples[i1] - samples[i0]) * frac;
            if (v > 1.0f)  v = 1.0f;
            if (v < -1.0f) v = -1.0f;

            float x = rect->x + (float)i * colw;
            float y = center - v * half;
            if (y < rect->y)       y = rect->y;
            if (y > rect->y + rect->h) y = rect->y + rect->h;
            if (i > 0)
                SDL_RenderLine(r, prev_x, prev_y, x, y);
            prev_x = x;
            prev_y = y;
        }
    }

    SDL_SetRenderDrawColor(r, 60, 70, 82, 255);
    SDL_RenderRect(r, rect);
}
