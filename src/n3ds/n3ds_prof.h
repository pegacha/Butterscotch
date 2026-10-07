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
// citro2d batch flushes this frame (each a GPU draw call: blend mode, texture and target changes force one).
extern u32 gN3DSProfFlushes;

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

// ===[ Frame profiler (build option N3DS_FRAME_PROFILER) ]===
// Exclusive time per zone: entering a zone pauses the one it is nested in, so a frame's time splits cleanly into the
// game's phases (step, drawing the views, the GUI, ...) and the renderer work inside them (sprites, tiles, text,
// surfaces, GPU flushes, texture loads). main.c logs the per-room totals (Profile: lines in log.txt). Main thread
// only; off in normal builds (N3DS_ZONE compiles to nothing).
typedef enum {
    N3DS_ZONE_STEP,          // Runner_step: GML step events, collisions, alarms (minus renderer zones inside)
    N3DS_ZONE_FRAME_START,   // drawPre + beginFrame
    N3DS_ZONE_VIEWS,         // Runner_drawViews: GML draw events, instance sorting, tile and layer walking
    N3DS_ZONE_POST,          // endFrameInit + drawPost + endFrameEnd (the game draws its surfaces to the screen)
    N3DS_ZONE_GUI,           // Runner_drawGUI: GML Draw GUI events
    N3DS_ZONE_OVERLAYS,      // live map, debug monitor, Saving..., frozen top screen
    N3DS_ZONE_ROOM_CHANGE,
    N3DS_ZONE_AFTER_FRAME,   // texture collection, file write-back
    N3DS_ZONE_SPRITE,        // draw_sprite(_ext)
    N3DS_ZONE_SPRITE_PART,   // draw_sprite_part(_ext), draw_background_part
    N3DS_ZONE_SPRITE_POS,
    N3DS_ZONE_TILE,          // room tiles (counted, not timed)
    N3DS_ZONE_TILED,         // draw_*_tiled
    N3DS_ZONE_TEXT,
    N3DS_ZONE_SHAPES,        // rectangles, lines, triangles, primitives
    N3DS_ZONE_SURFACE_DRAW,  // draw_surface*
    N3DS_ZONE_TARGET,        // surface_set_target / reset, surface create / free / copy, draw_clear
    N3DS_ZONE_VIEW_SETUP,    // view and GUI projections, matrices
    N3DS_ZONE_STATE,         // blend mode, alpha test (each can force a GPU flush)
    N3DS_ZONE_GPU_FLUSH,     // citro2d batch submissions
    N3DS_ZONE_PAGE_LOAD,     // atlas page / sprite sheet loads and uploads
    N3DS_ZONE_FRAME_WAIT,    // C3D_FrameBegin: the previous frame's GPU work and the vblank
    N3DS_ZONE_COUNT
} N3DSZone;

#ifdef N3DS_FRAME_PROFILER
extern u64 gN3DSZoneTicks[N3DS_ZONE_COUNT];
extern u32 gN3DSZoneCalls[N3DS_ZONE_COUNT];
extern int gN3DSZoneStack[64];
extern int gN3DSZoneDepth;
extern u64 gN3DSZoneMark;

static inline int N3DSZone_enter(int zone) {
    u64 now = svcGetSystemTick();
    if (gN3DSZoneDepth > 0) gN3DSZoneTicks[gN3DSZoneStack[gN3DSZoneDepth - 1]] += now - gN3DSZoneMark;
    if (gN3DSZoneDepth < 64) gN3DSZoneStack[gN3DSZoneDepth] = zone;
    gN3DSZoneDepth++;
    gN3DSZoneCalls[zone]++;
    gN3DSZoneMark = now;
    return zone;
}

static inline void N3DSZone_exit(int* zone) {
    (void) zone;
    u64 now = svcGetSystemTick();
    if (gN3DSZoneDepth > 0 && gN3DSZoneDepth <= 64) gN3DSZoneTicks[gN3DSZoneStack[gN3DSZoneDepth - 1]] += now - gN3DSZoneMark;
    if (gN3DSZoneDepth > 0) gN3DSZoneDepth--;
    gN3DSZoneMark = now;
}

#define N3DS_ZONE(zone) int n3dsZone_ __attribute__((cleanup(N3DSZone_exit), unused)) = N3DSZone_enter(zone)
// Calls only, for things done thousands of times a frame: reading the clock is a syscall (and Azahar charges 150
// cycles for each), so timing them would mostly measure the profiler. Their time stays in the enclosing zone.
#define N3DS_ZONE_COUNT_ONLY(zone) (gN3DSZoneCalls[zone]++)
#else
#define N3DS_ZONE(zone) ((void) 0)
#define N3DS_ZONE_COUNT_ONLY(zone) ((void) 0)
#endif
