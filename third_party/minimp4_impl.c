// minimp4_impl.c — the one place MINIMP4_IMPLEMENTATION is defined, so
// decoder.c stays a fast rebuild and minimp4's internals compile with -w
// (same deal as dr_libs_impl.c).
//
// minimp4 uses strdup, which is POSIX, not C11 — under -std=c11 it needs the
// feature-test macro (only here; -std=gnu11 users wouldn't see this).

#define _POSIX_C_SOURCE 200809L

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
