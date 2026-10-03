#pragma once

// Main-thread time spent in the slow things a frame can hit (SD reads, texture uploads, game file I/O, audio), so a
// stall on hardware says where it went. main.c reads and clears the counters once per frame.

#include <3ds.h>

typedef enum {
    N3DS_PROF_IO,    // renderer SD reads (atlas pages, sprite sheets)
    N3DS_PROF_TEX,   // texture imports, uploads and surface allocations
    N3DS_PROF_FS,    // the game's own file operations (ini, saves, file_exists misses)
    N3DS_PROF_AUDIO, // audio system calls (play, stop, update)
    N3DS_PROF_PREWARM,
    N3DS_PROF_COUNT
} N3DSProfCounter;

extern u64 gN3DSProfTicks[N3DS_PROF_COUNT];

static inline u64 N3DSProf_begin(void) {
    return svcGetSystemTick();
}

// Worker threads don't count: the counters describe what the main thread waited for.
static inline void N3DSProf_end(N3DSProfCounter counter, u64 start) {
    if (threadGetCurrent() == NULL) gN3DSProfTicks[counter] += svcGetSystemTick() - start;
}

static inline double N3DSProf_ms(u64 ticks) {
    return (double) ticks * 1000.0 / (double) SYSCLOCK_ARM11;
}
