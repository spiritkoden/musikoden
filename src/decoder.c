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

extern int stb_vorbis_decode_filename(const char *filename, int *channels,
                                      int *sample_rate, short **output);

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

static void i16_to_f32(const short *in, size_t count, float *out) {
    for (size_t i = 0; i < count; ++i)
        out[i] = (float)in[i] / 32768.0f;
}

static bool load_mp3(const char *path, DecodedAudio *out) {
    drmp3_config cfg;
    memset(&cfg, 0, sizeof cfg);
    drmp3_uint64 frames = 0;
    float *s = drmp3_open_file_and_read_pcm_frames_f32(path, &cfg, &frames, NULL);
    if (!s || frames == 0 || cfg.channels == 0) {
        free(s);
        return false;
    }
    out->samples     = s;
    out->frames      = (uint64_t)frames;
    out->sample_rate = (uint32_t)cfg.sampleRate;
    out->channels    = (uint32_t)cfg.channels;
    return true;
}

static bool load_flac(const char *path, DecodedAudio *out) {
    unsigned int channels = 0, rate = 0;
    drflac_uint64 frames = 0;
    float *s = drflac_open_file_and_read_pcm_frames_f32(path, &channels, &rate,
                                                        &frames, NULL);
    if (!s || frames == 0 || channels == 0) {
        drflac_free(s, NULL);
        return false;
    }
    out->samples     = s;
    out->frames      = (uint64_t)frames;
    out->sample_rate = (uint32_t)rate;
    out->channels    = (uint32_t)channels;
    return true;
}

static bool load_wav(const char *path, DecodedAudio *out) {
    unsigned int channels = 0, rate = 0;
    drwav_uint64 frames = 0;
    float *s = drwav_open_file_and_read_pcm_frames_f32(path, &channels, &rate,
                                                       &frames, NULL);
    if (!s || frames == 0 || channels == 0) {
        drwav_free(s, NULL);
        return false;
    }
    out->samples     = s;
    out->frames      = (uint64_t)frames;
    out->sample_rate = (uint32_t)rate;
    out->channels    = (uint32_t)channels;
    return true;
}

static bool load_ogg(const char *path, DecodedAudio *out) {
    int channels = 0, rate = 0;
    short *pcm16 = NULL;
    int frames = stb_vorbis_decode_filename(path, &channels, &rate, &pcm16);
    if (frames <= 0 || !pcm16 || channels <= 0) {
        free(pcm16);
        return false;
    }
    size_t count    = (size_t)frames * (size_t)channels;
    float  *samples = (float *)malloc(count * sizeof(float));
    if (!samples) {
        free(pcm16);
        return false;
    }
    i16_to_f32(pcm16, count, samples);
    free(pcm16);

    out->samples     = samples;
    out->frames      = (uint64_t)frames;
    out->sample_rate = (uint32_t)rate;
    out->channels    = (uint32_t)channels;
    return true;
}

static int mp4_read_cb(int64_t offset, void *buffer, size_t size, void *token) {
    FILE *f = (FILE *)token;
    if (offset < 0 || offset > (int64_t)LONG_MAX) return 1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 1;
    return fread(buffer, 1, size, f) != size;
}

static bool oti_is_aac(unsigned oti) {
    return oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 ||
           oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE ||
           oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE ||
           oti == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE;
}

static bool mp4_sample_read(MP4D_demux_t *mp4, FILE *f, unsigned t, unsigned n,
                            unsigned char **buf, size_t *cap, unsigned *bytes) {
    unsigned ts = 0, dur = 0, frame_bytes = 0;
    MP4D_file_offset_t off = MP4D_frame_offset(mp4, t, n, &frame_bytes, &ts, &dur);
    (void)ts;
    (void)dur;
    if (frame_bytes == 0) return false;
    if ((size_t)frame_bytes > *cap) {
        unsigned char *grown = (unsigned char *)realloc(*buf, (size_t)frame_bytes);
        if (!grown) return false;
        *buf = grown;
        *cap = (size_t)frame_bytes;
    }
    if (mp4_read_cb((int64_t)off, *buf, (size_t)frame_bytes, f)) return false;
    *bytes = frame_bytes;
    return true;
}

static bool load_m4a(const char *path, DecodedAudio *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    bool           ok          = false;
    bool           mp4_open    = false;
    NeAACDecHandle dec         = NULL;
    unsigned char *scratch     = NULL;
    size_t         scratch_cap = 0;
    float         *samples     = NULL;
    size_t         used = 0, cap = 0;
    unsigned       channels    = 0;
    MP4D_demux_t   mp4;
    memset(&mp4, 0, sizeof mp4);

    if (fseek(f, 0, SEEK_END) != 0) goto done;
    long fsize = ftell(f);
    if (fsize <= 0 || !MP4D_open(&mp4, mp4_read_cb, f, (int64_t)fsize)) goto done;
    mp4_open = true;

    unsigned t;
    for (t = 0; t < mp4.track_count; ++t) {
        if (mp4.track[t].handler_type == MP4D_HANDLER_TYPE_SOUN &&
            oti_is_aac(mp4.track[t].object_type_indication))
            break;
    }
    if (t == mp4.track_count) {
        SDL_Log("decoder: %s: no AAC audio track", path);
        goto done;
    }
    MP4D_track_t *tr = &mp4.track[t];

    dec = NeAACDecOpen();
    if (!dec) goto done;
    NeAACDecConfiguration *cfg = NeAACDecGetCurrentConfiguration(dec);
    cfg->outputFormat = FAAD_FMT_FLOAT;

    unsigned long rate = 0;
    unsigned char ch   = 0;
    if (tr->dsi_bytes > 0) {
        if (NeAACDecInit2(dec, tr->dsi, (unsigned long)tr->dsi_bytes,
                          &rate, &ch) < 0) {
            SDL_Log("decoder: %s: faad2 rejected the AAC config", path);
            goto done;
        }
    } else {
        unsigned nbytes = 0;
        if (!mp4_sample_read(&mp4, f, t, 0, &scratch, &scratch_cap, &nbytes) ||
            NeAACDecInit(dec, scratch, (unsigned long)nbytes, &rate, &ch) < 0) {
            SDL_Log("decoder: %s: faad2 init failed", path);
            goto done;
        }
    }
    if (rate == 0) goto done;

    if (tr->timescale) {
        uint64_t dur64 = ((uint64_t)tr->duration_hi << 32) | (uint64_t)tr->duration_lo;
        double   secs  = (double)dur64 / (double)tr->timescale;
        cap = (size_t)(secs * (double)rate) + 2048u;
        samples = (float *)malloc(cap * sizeof(float));
        if (!samples) goto done;
    }

    for (unsigned i = 0; i < tr->sample_count; ++i) {
        unsigned nbytes = 0;
        if (!mp4_sample_read(&mp4, f, t, i, &scratch, &scratch_cap, &nbytes))
            break;

        NeAACDecFrameInfo fi;
        const float *pcm = (const float *)NeAACDecDecode(dec, &fi, scratch,
                                                         (unsigned long)nbytes);
        if (fi.error || !pcm) continue;
        size_t ns = (size_t)fi.samples;
        if (ns == 0) continue;

        if (used + ns > cap) {
            size_t ncap = cap * 2 + ns + 2048u;
            float *grown = (float *)realloc(samples, ncap * sizeof(float));
            if (!grown) goto done;
            samples = grown;
            cap     = ncap;
        }
        // HE-AACv2 upmixes mono to stereo after init, so fi.channels can
        // disagree with what NeAACDecInit*() reported
        if (used == 0 && fi.channels > 0) channels = (unsigned)fi.channels;

        memcpy(samples + used, pcm, ns * sizeof(float));
        used += ns;
    }

    if (used == 0 || channels == 0) goto done;

    float *shrunk = (float *)realloc(samples, used * sizeof(float));
    if (shrunk) samples = shrunk;
    out->samples     = samples;
    out->frames      = (uint64_t)(used / (size_t)channels);
    out->sample_rate = (uint32_t)rate;
    out->channels    = (uint32_t)channels;
    samples = NULL;
    ok = true;

done:
    if (dec) NeAACDecClose(dec);
    if (mp4_open) MP4D_close(&mp4);
    free(scratch);
    free(samples);
    fclose(f);
    return ok;
}

bool decoded_audio_load(const char *path, DecodedAudio *out) {
    memset(out, 0, sizeof *out);

    bool ok = false;
    if      (ext_is(path, "mp3"))  ok = load_mp3(path, out);
    else if (ext_is(path, "flac")) ok = load_flac(path, out);
    else if (ext_is(path, "ogg") || ext_is(path, "oga"))
        ok = load_ogg(path, out);
    else if (ext_is(path, "wav"))  ok = load_wav(path, out);
    else if (ext_is(path, "m4a"))  ok = load_m4a(path, out);
    else {
        SDL_Log("decoder: unsupported extension: %s", path);
        return false;
    }

    if (!ok) {
        SDL_Log("decoder: failed to decode: %s", path);
        memset(out, 0, sizeof *out);
    }
    return ok;
}

void decoded_audio_free(DecodedAudio *a) {
    if (!a) return;
    // every loader hands back plain malloc memory (dr_* default allocator,
    // direct malloc for ogg, a copy of faad2's output for m4a)
    if (a->samples)
        free(a->samples);
    memset(a, 0, sizeof *a);
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
