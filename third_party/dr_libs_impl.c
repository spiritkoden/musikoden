// dr_libs_impl.c — the single translation unit that compiles the dr_libs
// single-header implementations (dr_mp3, dr_flac, dr_wav).
//
// The headers are stb-style: including them elsewhere yields declarations only.
// Exactly one TU must define the *_IMPLEMENTATION macros. Keeping them here
// (rather than in src/decoder.c) keeps decoder.c fast to recompile.
//
// stb_vorbis is separate: it is a .c file compiled directly (see Makefile).

#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION

#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"
