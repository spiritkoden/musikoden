#include "decoder.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"

#include "minimp4.h"
#include <neaacdec.h>

// stb_vorbis is a single .c file with no header shipped in this tree; declare
// the small streaming surface we use.
typedef struct stb_vorbis stb_vorbis;
typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;

extern stb_vorbis *stb_vorbis_open_filename(const char *filename, int *error,
                                            const void *alloc);
extern void stb_vorbis_close(stb_vorbis *f);
extern stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
extern unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f);
extern int stb_vorbis_get_samples_float_interleaved(stb_vorbis *f, int channels,
                                                    float *buffer, int num_floats);
extern int stb_vorbis_seek(stb_vorbis *f, unsigned int sample_number);

typedef enum {
    DEC_MP3,
    DEC_FLAC,
    DEC_WAV,
    DEC_OGG,
    DEC_M4A,
} DecKind;

typedef struct {
    FILE           *f;
    MP4D_demux_t    mp4;
    bool            mp4_open;
    NeAACDecHandle  dec;
    unsigned        track;
    unsigned        next_sample;   // next MP4 sample (AAC frame) to decode
    unsigned        sample_count;
    uint64_t        skip;          // frames to drop from the next decoded chunk
    unsigned char  *scratch;
    size_t          scratch_cap;
    const float    *pcm;           // points into faad2's internal buffer
    size_t          pcm_frames;    // frames available in pcm
    size_t          pcm_pos;       // frames already consumed from pcm
    unsigned        out_channels;  // channels reported by the decoded frame
} M4A;

struct Decoder {
    DecKind  kind;
    uint32_t channels;
    uint32_t sample_rate;
    uint64_t frames;
    union {
        drmp3       mp3;
        drflac     *flac;
        drwav       wav;
        stb_vorbis *vorbis;
        M4A         m4a;
    } u;
};

static const char *file_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

static bool ext_is(const char *path, const char *want) {
    const char *ext = file_ext(path);
    for (; *ext && *want; ++ext, ++want) {
        if (tolower((unsigned char)*ext) != tolower((unsigned char)*want))
            return false;
    }
    return *ext == '\0' && *want == '\0';
}

bool is_audio_file(const char *path) {
    return ext_is(path, "mp3") || ext_is(path, "flac") ||
           ext_is(path, "ogg") || ext_is(path, "oga") ||
           ext_is(path, "wav") || ext_is(path, "m4a");
}

// ---------------------------------------------------------------------------
// MP3
// ---------------------------------------------------------------------------

static Decoder *open_mp3(const char *path, AudioInfo *info) {
    Decoder *d = (Decoder *)calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->kind = DEC_MP3;

    if (!drmp3_init_file(&d->u.mp3, path, NULL)) {
        free(d);
        return NULL;
    }

    uint64_t frames = drmp3_get_pcm_frame_count(&d->u.mp3);
    d->channels    = (uint32_t)d->u.mp3.channels;
    d->sample_rate = (uint32_t)d->u.mp3.sampleRate;
    d->frames      = frames;

    info->frames      = frames;
    info->sample_rate = d->sample_rate;
    info->channels    = d->channels;
    return d;
}

static uint32_t read_mp3(Decoder *d, float *dst, uint32_t frames) {
    return (uint32_t)drmp3_read_pcm_frames_f32(&d->u.mp3, frames, dst);
}

static bool seek_mp3(Decoder *d, uint64_t frame) {
    return drmp3_seek_to_pcm_frame(&d->u.mp3, frame) != 0;
}

static void close_mp3(Decoder *d) {
    drmp3_uninit(&d->u.mp3);
}

// ---------------------------------------------------------------------------
// FLAC
// ---------------------------------------------------------------------------

static Decoder *open_flac(const char *path, AudioInfo *info) {
    drflac *f = drflac_open_file(path, NULL);
    if (!f)
        return NULL;

    Decoder *d = (Decoder *)calloc(1, sizeof *d);
    if (!d) {
        drflac_close(f);
        return NULL;
    }
    d->kind        = DEC_FLAC;
    d->u.flac      = f;
    d->channels    = (uint32_t)f->channels;
    d->sample_rate = (uint32_t)f->sampleRate;
    d->frames      = (uint64_t)f->totalPCMFrameCount;

    info->frames      = d->frames;
    info->sample_rate = d->sample_rate;
    info->channels    = d->channels;
    return d;
}

static uint32_t read_flac(Decoder *d, float *dst, uint32_t frames) {
    return (uint32_t)drflac_read_pcm_frames_f32(d->u.flac, frames, dst);
}

static bool seek_flac(Decoder *d, uint64_t frame) {
    return drflac_seek_to_pcm_frame(d->u.flac, frame) != 0;
}

static void close_flac(Decoder *d) {
    drflac_close(d->u.flac);
}

// ---------------------------------------------------------------------------
// WAV
// ---------------------------------------------------------------------------

static Decoder *open_wav(const char *path, AudioInfo *info) {
    Decoder *d = (Decoder *)calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->kind = DEC_WAV;

    if (!drwav_init_file(&d->u.wav, path, NULL)) {
        free(d);
        return NULL;
    }
    d->channels    = (uint32_t)d->u.wav.channels;
    d->sample_rate = (uint32_t)d->u.wav.sampleRate;
    d->frames      = (uint64_t)d->u.wav.totalPCMFrameCount;

    info->frames      = d->frames;
    info->sample_rate = d->sample_rate;
    info->channels    = d->channels;
    return d;
}

static uint32_t read_wav(Decoder *d, float *dst, uint32_t frames) {
    return (uint32_t)drwav_read_pcm_frames_f32(&d->u.wav, frames, dst);
}

static bool seek_wav(Decoder *d, uint64_t frame) {
    return drwav_seek_to_pcm_frame(&d->u.wav, frame) != 0;
}

static void close_wav(Decoder *d) {
    drwav_uninit(&d->u.wav);
}

// ---------------------------------------------------------------------------
// Ogg Vorbis
// ---------------------------------------------------------------------------

static Decoder *open_ogg(const char *path, AudioInfo *info) {
    int error = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &error, NULL);
    if (!v)
        return NULL;

    Decoder *d = (Decoder *)calloc(1, sizeof *d);
    if (!d) {
        stb_vorbis_close(v);
        return NULL;
    }
    d->kind   = DEC_OGG;
    d->u.vorbis = v;

    stb_vorbis_info vi = stb_vorbis_get_info(v);
    d->channels    = (uint32_t)vi.channels;
    d->sample_rate = vi.sample_rate;
    d->frames      = (uint64_t)stb_vorbis_stream_length_in_samples(v);

    info->frames      = d->frames;
    info->sample_rate = d->sample_rate;
    info->channels    = d->channels;
    return d;
}

static uint32_t read_ogg(Decoder *d, float *dst, uint32_t frames) {
    int got = stb_vorbis_get_samples_float_interleaved(
        d->u.vorbis, (int)d->channels, dst, (int)(frames * d->channels));
    if (got <= 0)
        return 0;
    return (uint32_t)got;
}

static bool seek_ogg(Decoder *d, uint64_t frame) {
    return stb_vorbis_seek(d->u.vorbis, (unsigned int)frame) != 0;
}

static void close_ogg(Decoder *d) {
    stb_vorbis_close(d->u.vorbis);
}

// ---------------------------------------------------------------------------
// M4A / AAC (minimp4 + faad2)
// ---------------------------------------------------------------------------

static int m4a_read_cb(int64_t offset, void *buffer, size_t size, void *token) {
    FILE *f = (FILE *)token;
    if (offset < 0 || offset > (int64_t)LONG_MAX)
        return 1;
    if (fseek(f, (long)offset, SEEK_SET) != 0)
        return 1;
    return fread(buffer, 1, size, f) != size;
}

static bool oti_is_aac(unsigned oti) {
    return oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 ||
           oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE ||
           oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE ||
           oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE;
}

static bool m4a_sample_read(M4A *m, unsigned n, unsigned char **buf,
                            size_t *cap, unsigned *bytes) {
    unsigned ts = 0, dur = 0, frame_bytes = 0;
    MP4D_file_offset_t off = MP4D_frame_offset(&m->mp4, m->track, n,
                                               &frame_bytes, &ts, &dur);
    (void)ts;
    (void)dur;
    if (frame_bytes == 0)
        return false;
    if ((size_t)frame_bytes > *cap) {
        unsigned char *grown = (unsigned char *)realloc(*buf, (size_t)frame_bytes);
        if (!grown)
            return false;
        *buf = grown;
        *cap = (size_t)frame_bytes;
    }
    if (m4a_read_cb((int64_t)off, *buf, (size_t)frame_bytes, m->f))
        return false;
    *bytes = frame_bytes;
    return true;
}

// Decode one MP4 sample into m->pcm (borrowed from faad2; valid until the
// next NeAACDecDecode() call).
static bool m4a_decode_sample(M4A *m, unsigned n) {
    unsigned nbytes = 0;
    if (!m4a_sample_read(m, n, &m->scratch, &m->scratch_cap, &nbytes)) {
        m->pcm = NULL;
        m->pcm_frames = m->pcm_pos = 0;
        return false;
    }

    NeAACDecFrameInfo fi;
    const float *pcm = (const float *)NeAACDecDecode(
        m->dec, &fi, m->scratch, (unsigned long)nbytes);
    // fi.samples is the TOTAL interleaved sample count (frames * channels),
    // matching the size of the returned buffer in floats.
    unsigned ch = fi.channels ? fi.channels : 1;
    if (fi.error || !pcm || fi.samples == 0) {
        m->pcm = NULL;
        m->pcm_frames = m->pcm_pos = 0;
        return false;
    }
    m->pcm = pcm;
    m->pcm_frames = (size_t)fi.samples / ch;
    m->pcm_pos = 0;
    m->out_channels = (unsigned)fi.channels;
    return true;
}

static Decoder *open_m4a(const char *path, AudioInfo *info) {
    Decoder *d = (Decoder *)calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->kind = DEC_M4A;
    M4A *m = &d->u.m4a;
    memset(&m->mp4, 0, sizeof m->mp4);

    m->f = fopen(path, "rb");
    if (!m->f)
        goto fail;

    if (fseek(m->f, 0, SEEK_END) != 0)
        goto fail;
    long fsize = ftell(m->f);
    if (fsize <= 0 || !MP4D_open(&m->mp4, m4a_read_cb, m->f, (int64_t)fsize))
        goto fail;
    m->mp4_open = true;

    unsigned t;
    for (t = 0; t < m->mp4.track_count; ++t) {
        if (m->mp4.track[t].handler_type == MP4D_HANDLER_TYPE_SOUN &&
            oti_is_aac(m->mp4.track[t].object_type_indication))
            break;
    }
    if (t == m->mp4.track_count) {
        SDL_Log("decoder: %s: no AAC audio track", path);
        goto fail;
    }
    m->track = t;
    MP4D_track_t *tr = &m->mp4.track[t];
    m->sample_count = tr->sample_count;

    m->dec = NeAACDecOpen();
    if (!m->dec)
        goto fail;
    NeAACDecConfiguration *cfg = NeAACDecGetCurrentConfiguration(m->dec);
    cfg->outputFormat = FAAD_FMT_FLOAT;

    unsigned long rate = 0;
    unsigned char ch   = 0;
    if (tr->dsi_bytes > 0) {
        if (NeAACDecInit2(m->dec, tr->dsi, (unsigned long)tr->dsi_bytes,
                          &rate, &ch) < 0) {
            SDL_Log("decoder: %s: faad2 rejected the AAC config", path);
            goto fail;
        }
    } else {
        unsigned nbytes = 0;
        if (!m4a_sample_read(m, 0, &m->scratch, &m->scratch_cap, &nbytes) ||
            NeAACDecInit(m->dec, m->scratch, (unsigned long)nbytes,
                         &rate, &ch) < 0) {
            SDL_Log("decoder: %s: faad2 init failed", path);
            goto fail;
        }
    }
    if (rate == 0)
        goto fail;

    // Decode leading samples until one yields audio, so the real output
    // channel count is known (HE-AACv2 upmixes mono to stereo after init) and
    // playback can start without a second open/init pass. A handful of
    // priming frames may decode to zero samples.
    unsigned tries = 0;
    while (m->pcm_frames == 0 && m->next_sample < m->sample_count &&
           tries < 64) {
        m4a_decode_sample(m, m->next_sample);
        m->next_sample++;
        tries++;
    }
    if (m->pcm_frames == 0) {
        SDL_Log("decoder: %s: first AAC frames did not decode", path);
        goto fail;
    }

    d->channels    = m->out_channels ? m->out_channels : (uint32_t)ch;
    d->sample_rate = (uint32_t)rate;

    // Total frames: each AAC frame decodes to a constant number of output
    // frames (known from the first decoded frame), so scale that up. Fall back
    // to the container duration when that's unavailable.
    uint64_t per_sample = (uint64_t)m->pcm_frames;
    if (per_sample != 0)
        d->frames = per_sample * (uint64_t)m->sample_count;
    else if (tr->timescale) {
        uint64_t dur = ((uint64_t)tr->duration_hi << 32) |
                       (uint64_t)tr->duration_lo;
        d->frames = (uint64_t)((double)dur * (double)rate /
                               (double)tr->timescale);
    } else {
        d->frames = 0;
    }

    info->frames      = d->frames;
    info->sample_rate = d->sample_rate;
    info->channels    = d->channels;
    return d;

fail:
    if (m->dec) NeAACDecClose(m->dec);
    if (m->mp4_open) MP4D_close(&m->mp4);
    free(m->scratch);
    if (m->f) fclose(m->f);
    free(d);
    return NULL;
}

static uint32_t read_m4a(Decoder *d, float *dst, uint32_t frames) {
    M4A *m = &d->u.m4a;
    const uint32_t ch = d->channels;
    uint32_t out = 0;

    while (out < frames) {
        if (m->pcm_pos >= m->pcm_frames) {
            if (m->next_sample >= m->sample_count)
                break;
            m4a_decode_sample(m, m->next_sample++);
            if (m->pcm_frames == 0)
                continue;
        }

        size_t avail = m->pcm_frames - m->pcm_pos;
        if (m->skip > 0) {
            if (m->skip >= avail) {
                m->skip -= avail;
                m->pcm_pos = m->pcm_frames;
                continue;
            }
            m->pcm_pos += (size_t)m->skip;
            m->skip = 0;
            avail = m->pcm_frames - m->pcm_pos;
        }

        size_t want = (size_t)(frames - out);
        size_t n = avail < want ? avail : want;
        memcpy(dst + (size_t)out * ch, m->pcm + m->pcm_pos * ch,
               n * ch * sizeof(float));
        m->pcm_pos += n;
        out += (uint32_t)n;
    }
    return out;
}

static bool seek_m4a(Decoder *d, uint64_t frame) {
    M4A *m = &d->u.m4a;
    MP4D_track_t *tr = &m->mp4.track[m->track];
    if (!tr->timescale || m->sample_count == 0)
        return false;

    // Target position in media-timescale units.
    uint64_t target = (uint64_t)((double)frame * (double)tr->timescale /
                                 (double)d->sample_rate);

    unsigned idx = 0;
#if MP4D_TIMESTAMPS_SUPPORTED
    if (tr->timestamp && tr->timestamp_count) {
        unsigned lo = 0, hi = tr->timestamp_count;
        while (lo + 1 < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if ((uint64_t)tr->timestamp[mid] <= target)
                lo = mid;
            else
                hi = mid;
        }
        idx = lo;
    }
#endif
    if (idx >= m->sample_count)
        idx = m->sample_count - 1;

    m->next_sample = idx;
    m->pcm = NULL;
    m->pcm_frames = m->pcm_pos = 0;

    // Drop frames so the first decoded sample lines up with `frame`.
    uint64_t frame0 = 0;
#if MP4D_TIMESTAMPS_SUPPORTED
    if (tr->timestamp && tr->timestamp_count && idx < tr->timestamp_count)
        frame0 = (uint64_t)((double)tr->timestamp[idx] * (double)d->sample_rate /
                            (double)tr->timescale + 0.5);
#endif
    m->skip = frame > frame0 ? frame - frame0 : 0;

    // faad2 keeps SBR/PS overlap state across frames; after an arbitrary
    // seek that state is stale and would smear into the next frames.
    NeAACDecPostSeekReset(m->dec, (long)frame);
    return true;
}

static void close_m4a(Decoder *d) {
    M4A *m = &d->u.m4a;
    if (m->dec) NeAACDecClose(m->dec);
    if (m->mp4_open) MP4D_close(&m->mp4);
    free(m->scratch);
    if (m->f) fclose(m->f);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

Decoder *decoder_open(const char *path, AudioInfo *info) {
    memset(info, 0, sizeof *info);
    if (ext_is(path, "mp3"))       return open_mp3(path, info);
    if (ext_is(path, "flac"))      return open_flac(path, info);
    if (ext_is(path, "wav"))       return open_wav(path, info);
    if (ext_is(path, "ogg") || ext_is(path, "oga"))
                                   return open_ogg(path, info);
    if (ext_is(path, "m4a"))       return open_m4a(path, info);

    SDL_Log("decoder: unsupported extension: %s", path);
    return NULL;
}

uint32_t decoder_read(Decoder *d, float *dst, uint32_t frames) {
    switch (d->kind) {
    case DEC_MP3:  return read_mp3(d, dst, frames);
    case DEC_FLAC: return read_flac(d, dst, frames);
    case DEC_WAV:  return read_wav(d, dst, frames);
    case DEC_OGG:  return read_ogg(d, dst, frames);
    case DEC_M4A:  return read_m4a(d, dst, frames);
    }
    return 0;
}

bool decoder_seek(Decoder *d, uint64_t frame) {
    switch (d->kind) {
    case DEC_MP3:  return seek_mp3(d, frame);
    case DEC_FLAC: return seek_flac(d, frame);
    case DEC_WAV:  return seek_wav(d, frame);
    case DEC_OGG:  return seek_ogg(d, frame);
    case DEC_M4A:  return seek_m4a(d, frame);
    }
    return false;
}

void decoder_close(Decoder *d) {
    if (!d)
        return;
    switch (d->kind) {
    case DEC_MP3:  close_mp3(d); break;
    case DEC_FLAC: close_flac(d); break;
    case DEC_WAV:  close_wav(d); break;
    case DEC_OGG:  close_ogg(d); break;
    case DEC_M4A:  close_m4a(d); break;
    }
    free(d);
}

void format_time(double seconds, char *buf, int bufsize) {
    if (seconds < 0) seconds = 0;
    long total = (long)seconds;
    long h = total / 3600;
    long m = (total / 60) % 60;
    long s = total % 60;
    if (h > 0)
        snprintf(buf, (size_t)bufsize, "%ld:%02ld:%02ld", h, m, s);
    else
        snprintf(buf, (size_t)bufsize, "%ld:%02ld", m, s);
}
