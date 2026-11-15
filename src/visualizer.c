#include "visualizer.h"

#include <stdlib.h>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include "decoder.h"

#define WAVE_BINS 4096

struct WaveScan {
    SDL_Thread   *thread;
    SDL_Mutex    *lock;
    SDL_AtomicInt cancel;
    char         *path;
    uint64_t      frames;
    uint32_t      channels;
    int           bins;
    float        *minv;
    float        *maxv;
    uint64_t      scanned_frames;
    bool          done;
};

// --- background scan -------------------------------------------------------

static int SDLCALL scan_thread(void *data) {
    WaveScan *s = (WaveScan *)data;

    AudioInfo info;
    Decoder *dec = decoder_open(s->path, &info);
    if (!dec) {
        SDL_LockMutex(s->lock);
        s->done = true;
        SDL_UnlockMutex(s->lock);
        return 0;
    }

    uint32_t ch = s->channels ? s->channels
                              : (info.channels ? info.channels : 1u);

    const uint32_t chunk = 4096u;
    float *buf = (float *)malloc((size_t)chunk * ch * sizeof(float));
    if (!buf) {
        decoder_close(dec);
        SDL_LockMutex(s->lock);
        s->done = true;
        SDL_UnlockMutex(s->lock);
        return 0;
    }

    const double total = (s->frames != 0) ? (double)s->frames : 1.0;
    const double scale = (double)s->bins / total;
    uint64_t pos = 0;

    while (!SDL_GetAtomicInt(&s->cancel)) {
        uint32_t got = decoder_read(dec, buf, chunk);
        if (got == 0)
            break;

        SDL_LockMutex(s->lock);
        for (uint32_t f = 0; f < got; ++f) {
            uint64_t g = pos + f;
            int b = (int)((double)g * scale);
            if (b < 0) b = 0;
            if (b >= s->bins) b = s->bins - 1;
            float m = 0.0f;
            for (uint32_t c = 0; c < ch; ++c)
                m += buf[(size_t)f * ch + c];
            m /= (float)ch;
            if (m < s->minv[b]) s->minv[b] = m;
            if (m > s->maxv[b]) s->maxv[b] = m;
        }
        pos += got;
        s->scanned_frames = pos;
        SDL_UnlockMutex(s->lock);
    }

    free(buf);
    decoder_close(dec);

    SDL_LockMutex(s->lock);
    s->done = true;
    SDL_UnlockMutex(s->lock);
    return 0;
}

WaveScan *wavescan_start(const char *path, uint64_t frames, uint32_t channels) {
    if (!path || !path[0])
        return NULL;

    WaveScan *s = (WaveScan *)calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->path     = SDL_strdup(path);
    s->frames   = frames;
    s->channels = channels;
    s->bins     = WAVE_BINS;
    s->minv     = (float *)malloc((size_t)WAVE_BINS * sizeof(float));
    s->maxv     = (float *)malloc((size_t)WAVE_BINS * sizeof(float));
    if (!s->path || !s->minv || !s->maxv) {
        SDL_free(s->path);
        free(s->minv);
        free(s->maxv);
        free(s);
        return NULL;
    }

    s->lock = SDL_CreateMutex();
    if (!s->lock) {
        SDL_free(s->path);
        free(s->minv);
        free(s->maxv);
        free(s);
        return NULL;
    }

    SDL_SetAtomicInt(&s->cancel, 0);
    s->thread = SDL_CreateThread(scan_thread, "musikoden-scan", s);
    if (!s->thread) {
        SDL_DestroyMutex(s->lock);
        SDL_free(s->path);
        free(s->minv);
        free(s->maxv);
        free(s);
        return NULL;
    }
    return s;
}

void wavescan_free(WaveScan *s) {
    if (!s)
        return;
    SDL_SetAtomicInt(&s->cancel, 1);
    if (s->thread)
        SDL_WaitThread(s->thread, NULL);
    SDL_DestroyMutex(s->lock);
    SDL_free(s->path);
    free(s->minv);
    free(s->maxv);
    free(s);
}

// --- overview --------------------------------------------------------------

void overview_free(WaveOverview *o) {
    if (!o)
        return;
    free(o->minv);
    free(o->maxv);
    o->minv = o->maxv = NULL;
    o->columns = 0;
}

double wavescan_build(WaveScan *s, WaveOverview *o, int columns) {
    if (columns < 1)
        columns = 1;

    if (o->columns != columns) {
        overview_free(o);
        o->minv = (float *)malloc((size_t)columns * sizeof(float));
        o->maxv = (float *)malloc((size_t)columns * sizeof(float));
        if (!o->minv || !o->maxv) {
            overview_free(o);
            return 0.0;
        }
        o->columns = columns;
    }

    if (!s) {
        for (int i = 0; i < columns; ++i)
            o->minv[i] = o->maxv[i] = 0.0f;
        return 0.0;
    }

    SDL_LockMutex(s->lock);
    const int bins = s->bins;
    const float *mn = s->minv;
    const float *mx = s->maxv;
    for (int i = 0; i < columns; ++i) {
        double a = (double)i * (double)bins / (double)columns;
        double b = (double)(i + 1) * (double)bins / (double)columns;
        int b0 = (int)a;
        int b1 = (int)(b + 0.9999999999);
        if (b1 <= b0) b1 = b0 + 1;
        if (b1 > bins) b1 = bins;
        if (b0 >= bins) b0 = bins - 1;

        float mnc = 0.0f, mxc = 0.0f;
        for (int k = b0; k < b1; ++k) {
            if (mn[k] < mnc) mnc = mn[k];
            if (mx[k] > mxc) mxc = mx[k];
        }
        o->minv[i] = mnc;
        o->maxv[i] = mxc;
    }

    double progress = 1.0;
    if (!s->done && s->frames != 0)
        progress = (double)s->scanned_frames / (double)s->frames;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    SDL_UnlockMutex(s->lock);
    return progress;
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
