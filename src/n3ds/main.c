#include "../data_win.h"
#include "../runner.h"
#include "../runner_keyboard.h"
#include "../utils.h"
#include "../vm.h"

#include "../gettime.h"
#include "../log.h"
#include "../noop_audio_system.h"
#include "../runner_gamepad.h"
#include "../runner_mouse.h"

#include "../input_recording.h"
#include "../overlay_file_system.h"
#include "n3ds_audio_system.h"
#include "n3ds_cached_file_system.h"
#include "n3ds_input.h"
#include "n3ds_am2r.h"
#include "n3ds_livemap.h"
#include "n3ds_pause.h"
#include "n3ds_platform_config.h"
#include "n3ds_prof.h"
#include "n3ds_renderer.h"
#include "n3ds_screen.h"
#include "n3ds_unimpl.h"

#include <3ds.h>
#include <citro2d.h>

#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define N3DS_LOADING_TEXT_SCALE 0.42f
#define N3DS_BOOT_LOG_MAX_LINES 6
#define N3DS_BOOT_LOG_LINE_CHARS 88
#define N3DS_TOTAL_VRAM_BYTES (6u * 1024u * 1024u)
// citro2d drops draws past this many objects per frame; AM2R rooms can draw a few thousand tiles.
#define N3DS_C2D_MAX_OBJECTS 16384
#define N3DS_TOP_SCREEN_W 400
#define N3DS_TOP_SCREEN_H 240

extern u32 __ctru_heap_size;

void N3DSLog_init(void);
void N3DSLog_close(void);
void N3DSLog_tick(void);

typedef struct {
    bool useCitro2D;
    C3D_RenderTarget* target;
    C2D_TextBuf textBuf;
    PrintConsole console;
    char statusLine[128];
    int chunkIndex;
    int totalChunks;
} N3DSLoadingScreen;

static char gN3DSBootLogLines[N3DS_BOOT_LOG_MAX_LINES][N3DS_BOOT_LOG_LINE_CHARS];
static int gN3DSBootLogLineCount = 0;

#define N3DS_MONITOR_LINES 8

u64 gN3DSProfTicks[N3DS_PROF_COUNT];

// Where one frame's time went, in ms. Step is the game logic (GML), wait is citro3d waiting for the GPU and VBlank,
// draw is building the GPU commands; IO, TX, FS and AU are slices of those (see n3ds_prof.h).
typedef enum {
    N3DS_FT_FRAME,
    N3DS_FT_STEP,
    N3DS_FT_WAIT,
    N3DS_FT_DRAW,
    N3DS_FT_IO,
    N3DS_FT_TEX,
    N3DS_FT_FS,
    N3DS_FT_AUDIO,
    N3DS_FT_PREWARM,
    N3DS_FT_COUNT
} N3DSFrameTime;

typedef struct {
    C2D_TextBuf textBuf;
    // Laid-out text, rebuilt only when a line changes (the values update a few times a second).
    char lines[N3DS_MONITOR_LINES][96];
    C2D_Text texts[N3DS_MONITOR_LINES];
    bool laidOut;
    double displayedFps;
    double displayed[N3DS_FT_COUNT];
    double displayedMaxFrameMs;
    double sampled[N3DS_FT_COUNT];
    double sampledMaxFrameMs;
    uint32_t sampledFrames;
    u64 sampleStartMs;
} N3DSDebugMonitor;

static bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void N3DS_formatDebugSize(char* out, size_t outSize, uint32_t bytes) {
    if (out == NULL || outSize == 0) return;

    double kib = (double) bytes / 1024.0;
    if (kib < 1024.0) {
        snprintf(out, outSize, "%.0f KB", kib);
        return;
    }

    snprintf(out, outSize, "%.2f MB", kib / 1024.0);
}

static void N3DSDebugMonitor_init(N3DSDebugMonitor* monitor) {
    if (monitor == NULL) return;
    memset(monitor, 0, sizeof(*monitor));
    monitor->textBuf = C2D_TextBufNew(1024);
    monitor->sampleStartMs = osGetTime();
}

static void N3DSDebugMonitor_free(N3DSDebugMonitor* monitor) {
    if (monitor == NULL) return;
    if (monitor->textBuf != NULL) {
        C2D_TextBufDelete(monitor->textBuf);
        monitor->textBuf = NULL;
    }
}

static void N3DSDebugMonitor_tickFrame(N3DSDebugMonitor* monitor, const double* frameTimes) {
    if (monitor == NULL) return;

    monitor->sampledFrames++;
    repeat(N3DS_FT_COUNT, i) monitor->sampled[i] += frameTimes[i];
    if (frameTimes[N3DS_FT_FRAME] > monitor->sampledMaxFrameMs) monitor->sampledMaxFrameMs = frameTimes[N3DS_FT_FRAME];
    u64 nowMs = osGetTime();
    u64 elapsedMs = nowMs - monitor->sampleStartMs;
    if (elapsedMs < 250) return;

    monitor->displayedFps = ((double) monitor->sampledFrames * 1000.0) / (double) elapsedMs;
    repeat(N3DS_FT_COUNT, i) {
        monitor->displayed[i] = monitor->sampled[i] / (double) monitor->sampledFrames;
        monitor->sampled[i] = 0.0;
    }
    monitor->displayedMaxFrameMs = monitor->sampledMaxFrameMs;
    monitor->sampledMaxFrameMs = 0.0;
    monitor->sampledFrames = 0;
    monitor->sampleStartMs = nowMs;
}

// On the bottom screen, or on the top one while the bottom one shows the game's pause screen.
static void N3DSDebugMonitor_draw(N3DSDebugMonitor* monitor, Runner* runner, Renderer* renderer, bool onTop) {
#ifdef N3DS_DISABLE_BOTTOM_SCREEN
    (void) monitor;
    (void) runner;
    (void) renderer;
    return;
#endif
    if (monitor == NULL || runner == NULL || renderer == NULL || monitor->textBuf == NULL) return;

    const char* roomName = "(none)";
    if (runner->currentRoom != NULL && runner->currentRoom->name != NULL && runner->currentRoom->name[0] != '\0') {
        roomName = runner->currentRoom->name;
    }

    uint32_t atlasVRAMBytes = N3DSRenderer_getResidentAtlasVRAMBytes(renderer);
    uint32_t atlasPageCount = N3DSRenderer_getResidentAtlasPageCount(renderer);
    uint32_t directVRAMBytes = N3DSRenderer_getResidentDirectAssetVRAMBytes(renderer);
    // The heap takes the whole app region up front, so the region's free space says nothing: ask malloc.
    struct mallinfo mi = mallinfo();

    char vramUsed[24], vramTotal[24], heapUsed[24], heapTotal[24], linearFree[24];
    N3DS_formatDebugSize(vramUsed, sizeof(vramUsed), atlasVRAMBytes + directVRAMBytes);
    N3DS_formatDebugSize(vramTotal, sizeof(vramTotal), N3DS_TOTAL_VRAM_BYTES);
    N3DS_formatDebugSize(heapUsed, sizeof(heapUsed), (uint32_t) mi.uordblks);
    N3DS_formatDebugSize(heapTotal, sizeof(heapTotal), __ctru_heap_size);
    N3DS_formatDebugSize(linearFree, sizeof(linearFree), linearSpaceFree());

    const double* t = monitor->displayed;
    char fpsLine[96], timeLine[96], sliceLine[96], vramLine[96], ramLine[96], unimplLine[64], roomLine[96];
    snprintf(fpsLine, sizeof(fpsLine), "FPS %.1f  %.1fms  max %.0f", monitor->displayedFps, t[N3DS_FT_FRAME], monitor->displayedMaxFrameMs);
    snprintf(timeLine, sizeof(timeLine), "S %.1f  W %.1f  D %.1f  P %.1f", t[N3DS_FT_STEP], t[N3DS_FT_WAIT], t[N3DS_FT_DRAW], t[N3DS_FT_PREWARM]);
    snprintf(sliceLine, sizeof(sliceLine), "IO %.1f  TX %.1f  FS %.1f  AU %.1f", t[N3DS_FT_IO], t[N3DS_FT_TEX], t[N3DS_FT_FS], t[N3DS_FT_AUDIO]);
    snprintf(vramLine, sizeof(vramLine), "VRAM %s/%s  AT %lu", vramUsed, vramTotal, (unsigned long) atlasPageCount);
    snprintf(ramLine, sizeof(ramLine), "RAM %s/%s  L %s", heapUsed, heapTotal, linearFree);
    snprintf(unimplLine, sizeof(unimplLine), "UNIMPL %lu", (unsigned long) N3DS_unimplCount());
    snprintf(roomLine, sizeof(roomLine), "R %.28s", roomName);

    N3DSRenderer_beginScreenOverlay(renderer, onTop);
    const float boxX = onTop ? 172.0f : 92.0f;
    const float boxY = 8.0f;
    const float boxW = 220.0f;
    const float boxH = 127.0f;
    const float textX = boxX + 7.0f;
    C2D_DrawRectSolid(boxX, boxY, N3DS_OVERLAY_DEPTH, boxW, boxH, C2D_Color32(8, 10, 16, 218));
    C2D_DrawRectSolid(boxX, boxY, N3DS_OVERLAY_DEPTH, boxW, 2.0f, C2D_Color32(77, 118, 255, 245));
    C2D_DrawRectSolid(boxX, boxY + boxH - 1.0f, N3DS_OVERLAY_DEPTH, boxW, 1.0f, C2D_Color32(32, 46, 78, 230));
    // System-font text, one quad per character (the old per-pixel bitmap font cost ~2000 quads a frame, half of
    // citro2d's per-frame object budget).
    const char* lines[N3DS_MONITOR_LINES] = { "DBG", fpsLine, timeLine, sliceLine, vramLine, ramLine, unimplLine, roomLine };
    const float xs[N3DS_MONITOR_LINES] = { textX, boxX + 42.0f, textX, textX, textX, textX, textX, textX };
    const float ys[N3DS_MONITOR_LINES] = { 5.0f, 5.0f, 22.0f, 39.0f, 56.0f, 73.0f, 90.0f, 107.0f };
    const u32 colors[N3DS_MONITOR_LINES] = {
        C2D_Color32(255, 255, 255, 255), C2D_Color32(196, 230, 255, 255), C2D_Color32(196, 230, 255, 255),
        C2D_Color32(146, 188, 236, 255), C2D_Color32(196, 230, 255, 255), C2D_Color32(196, 230, 255, 255),
        C2D_Color32(255, 196, 196, 255), C2D_Color32(255, 230, 163, 255),
    };
    bool changed = !monitor->laidOut;
    repeat(N3DS_MONITOR_LINES, i) {
        if (strcmp(monitor->lines[i], lines[i]) != 0) changed = true;
    }
    if (changed) {
        C2D_TextBufClear(monitor->textBuf);
        repeat(N3DS_MONITOR_LINES, i) {
            snprintf(monitor->lines[i], sizeof(monitor->lines[i]), "%s", lines[i]);
            C2D_TextParse(&monitor->texts[i], monitor->textBuf, monitor->lines[i]);
            C2D_TextOptimize(&monitor->texts[i]);
        }
        monitor->laidOut = true;
    }
    repeat(N3DS_MONITOR_LINES, i) {
        C2D_DrawText(&monitor->texts[i], C2D_WithColor, xs[i], boxY + ys[i], N3DS_OVERLAY_DEPTH, 0.42f, 0.42f, colors[i]);
    }
    N3DSRenderer_endScreenOverlay(renderer);
}

// "Saving..." in the top screen's bottom-right corner while the game's files have changes not yet on the SD card
// (the second they are left to settle, then the write), held a little longer so it never just flickers.
#define N3DS_SAVING_HOLD_MS 600u

typedef struct {
    C2D_TextBuf buf;
    C2D_Text text;
    u64 visibleUntilMs;
} N3DSSavingIndicator;

static void N3DSSavingIndicator_draw(N3DSSavingIndicator* indicator, Renderer* renderer, bool saving) {
    u64 nowMs = osGetTime();
    if (saving) indicator->visibleUntilMs = nowMs + N3DS_SAVING_HOLD_MS;
    if (nowMs >= indicator->visibleUntilMs) return;
    if (indicator->buf == NULL) {
        indicator->buf = C2D_TextBufNew(32);
        C2D_TextParse(&indicator->text, indicator->buf, "Saving...");
        C2D_TextOptimize(&indicator->text);
    }
    const float scale = 0.4f;
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&indicator->text, scale, scale, &w, &h);
    float x = (float) N3DS_TOP_SCREEN_W - w - 6.0f, y = (float) N3DS_TOP_SCREEN_H - h - 4.0f;
    N3DSRenderer_beginScreenOverlay(renderer, true);
    C2D_DrawText(&indicator->text, C2D_WithColor, x + 1.0f, y + 1.0f, N3DS_OVERLAY_DEPTH, scale, scale, C2D_Color32(0, 0, 0, 160));
    C2D_DrawText(&indicator->text, C2D_WithColor, x, y, N3DS_OVERLAY_DEPTH, scale, scale, C2D_Color32(255, 255, 255, 210));
    N3DSRenderer_endScreenOverlay(renderer);
}

// The VM calls a builtin over a game script of the same name, for GameMaker 2.3+ games that carry compatibility
// scripts for newer builtins. A game made before 2.3 can't be doing that: its script is the real thing (AM2R's
// string_split(str, sep, index) returns one part; the GMS2 builtin returns an array, and every button hint in the
// pause screen read "<array...>").
static void N3DS_preferGameScripts(VMContext* vm, DataWin* dataWin) {
    if (DataWin_isVersionAtLeast(dataWin, 2, 3, 0, 0)) return;
    for (uint32_t i = 0; i < vm->funcCallCacheCount; i++) {
        if (vm->funcCallCache[i].builtin == NULL) continue;
        const char* name = dataWin->func.functions[i].name;
        ptrdiff_t script = shgeti(vm->codeIndexByName, (char*) name);
        if (script < 0) continue;
        vm->funcCallCache[i].builtin = NULL;
        vm->funcCallCache[i].scriptCodeIndex = vm->codeIndexByName[script].value;
        logInfo("VM: calls to %s go to the game's own script\n", name);
    }
}

static char* chooseDataWinPath(void) {
    if (fileExists("romfs:/data.win")) return safeStrdup("romfs:/data.win");
    return safeStrdup(N3DS_SD_DIR "data.win");
}

static void N3DSLoadingScreen_free(N3DSLoadingScreen* screen) {
    if (screen == NULL) return;
    if (screen->textBuf != NULL) {
        C2D_TextBufDelete(screen->textBuf);
        screen->textBuf = NULL;
    }
    if (screen->target != NULL) {
        C3D_RenderTargetDelete(screen->target);
        screen->target = NULL;
    }
}

static void N3DS_appendBootLog(const char* message) {
    if (message == NULL || message[0] == '\0') return;

    if (gN3DSBootLogLineCount < N3DS_BOOT_LOG_MAX_LINES) {
        snprintf(
            gN3DSBootLogLines[gN3DSBootLogLineCount],
            sizeof(gN3DSBootLogLines[gN3DSBootLogLineCount]),
            "%s",
            message
        );
        gN3DSBootLogLineCount++;
        return;
    }

    repeat(N3DS_BOOT_LOG_MAX_LINES - 1, i) {
        snprintf(
            gN3DSBootLogLines[i],
            sizeof(gN3DSBootLogLines[i]),
            "%s",
            gN3DSBootLogLines[i + 1]
        );
    }
    snprintf(
        gN3DSBootLogLines[N3DS_BOOT_LOG_MAX_LINES - 1],
        sizeof(gN3DSBootLogLines[N3DS_BOOT_LOG_MAX_LINES - 1]),
        "%s",
        message
    );
}

static void N3DSLoadingScreen_draw(N3DSLoadingScreen* screen) {
    if (screen == NULL) return;

    float progress = 0.0f;
    if (screen->totalChunks > 0) {
        progress = (float) screen->chunkIndex / (float) screen->totalChunks;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
    }

    if (screen->useCitro2D && screen->target != NULL && screen->textBuf != NULL) {
        const float barX = 36.0f;
        const float barY = 146.0f;
        const float barW = 328.0f;
        const float barH = 18.0f;
        const float fillW = (barW - 4.0f) * progress;

        C2D_Text title;
        C2D_Text status;
        C2D_Text detail;
        char detailLine[64];
        snprintf(detailLine, sizeof(detailLine), "%d / %d chunks", screen->chunkIndex, screen->totalChunks);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(screen->target, C2D_Color32(8, 10, 14, 255));
        C2D_SceneBegin(screen->target);

        C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, 400.0f, 240.0f, C2D_Color32(8, 10, 14, 255));
        C2D_DrawRectSolid(barX, barY, 0.5f, barW, barH, C2D_Color32(42, 48, 60, 255));
        C2D_DrawRectSolid(barX + 2.0f, barY + 2.0f, 0.5f, fillW, barH - 4.0f, C2D_Color32(110, 224, 160, 255));

        C2D_TextBufClear(screen->textBuf);
        C2D_TextParse(&title, screen->textBuf, "Loading Game Data");
        C2D_TextParse(&status, screen->textBuf, screen->statusLine);
        C2D_TextParse(&detail, screen->textBuf, detailLine);
        C2D_TextOptimize(&title);
        C2D_TextOptimize(&status);
        C2D_TextOptimize(&detail);

        C2D_DrawText(&title, C2D_WithColor, 36.0f, 74.0f, 0.5f, 0.60f, 0.60f, C2D_Color32(255, 255, 255, 255));
        C2D_DrawText(&status, C2D_WithColor, 36.0f, 104.0f, 0.5f, N3DS_LOADING_TEXT_SCALE, N3DS_LOADING_TEXT_SCALE, C2D_Color32(210, 214, 224, 255));
        C2D_DrawText(&detail, C2D_WithColor, 36.0f, 172.0f, 0.5f, 0.34f, 0.34f, C2D_Color32(146, 153, 168, 255));

        int firstLogLine = gN3DSBootLogLineCount > 3 ? gN3DSBootLogLineCount - 3 : 0;
        float logY = 188.0f;
        for (int i = firstLogLine; i < gN3DSBootLogLineCount; ++i) {
            C2D_Text logLine;
            C2D_TextParse(&logLine, screen->textBuf, gN3DSBootLogLines[i]);
            C2D_TextOptimize(&logLine);
            C2D_DrawText(&logLine, C2D_WithColor, 36.0f, logY, 0.5f, 0.25f, 0.25f, C2D_Color32(164, 176, 192, 255));
            logY += 12.0f;
        }

        C3D_FrameEnd(0);
        gspWaitForVBlank();
        return;
    }

    int filled = (int) lroundf(progress * 24.0f);
    if (filled < 0) filled = 0;
    if (filled > 24) filled = 24;

    char bar[25];
    repeat(24, i) {
        bar[i] = (int) i < filled ? '#' : '-';
    }
    bar[24] = '\0';

    consoleSelect(&screen->console);
    printf("\x1b[2J");
    printf("\x1b[4;8HLoading Game Data");
    printf("\x1b[7;8H%s", screen->statusLine);
    printf("\x1b[10;8H[%s]", bar);
    printf("\x1b[12;8H%d / %d chunks", screen->chunkIndex, screen->totalChunks);
    printf("\x1b[15;8HPlease wait...");
    int firstLogLine = gN3DSBootLogLineCount > 5 ? gN3DSBootLogLineCount - 5 : 0;
    for (int i = firstLogLine; i < gN3DSBootLogLineCount; ++i) {
        printf("\x1b[%d;4H%s", 17 + (i - firstLogLine), gN3DSBootLogLines[i]);
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void N3DSLoadingScreen_set(N3DSLoadingScreen* screen, const char* statusLine, int chunkIndex, int totalChunks) {
    if (screen == NULL) return;
    snprintf(screen->statusLine, sizeof(screen->statusLine), "%s", statusLine != NULL ? statusLine : "");
    screen->chunkIndex = chunkIndex;
    screen->totalChunks = totalChunks;
    N3DSLoadingScreen_draw(screen);
}

static void N3DS_waitForStartExitScreen(N3DSLoadingScreen* screen, const char* statusLine) {
    N3DSLoadingScreen_set(screen, statusLine, 1, 1);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

static void N3DSDataWinProgressCallback(const char* chunkName, int chunkIndex, int totalChunks, MAYBE_UNUSED DataWin* dataWin, void* userData) {
    N3DSLoadingScreen* screen = (N3DSLoadingScreen*) userData;
    if (screen == NULL) return;

    char status[128];
    snprintf(status, sizeof(status), "Parsing chunk %.4s", chunkName != NULL ? chunkName : "----");
    N3DSLoadingScreen_set(screen, status, chunkIndex + 1, totalChunks);
}

static void N3DS_noopSetWindowTitle(MAYBE_UNUSED const char* title) {}
static void N3DS_noopSetWindowSize(MAYBE_UNUSED int32_t width, MAYBE_UNUSED int32_t height) {}
#ifndef N3DS_DEFAULT_SCREEN_MODE
#define N3DS_DEFAULT_SCREEN_MODE N3DS_SCREEN_WIDE
#endif
static N3DSScreenMode gScreenMode = N3DS_DEFAULT_SCREEN_MODE;
static int32_t gWindowW = N3DS_TOP_SCREEN_W;
static int32_t gWindowH = N3DS_TOP_SCREEN_H;

static const char* N3DS_screenModeName(N3DSScreenMode mode) {
    switch (mode) {
        case N3DS_SCREEN_WIDE: return "wide (more of the room, 1:1)";
        case N3DS_SCREEN_STRETCH: return "stretch";
        case N3DS_SCREEN_1X: return "1x";
        case N3DS_SCREEN_2X: return "2x";
        default: return "pillarbox (1:1)";
    }
}

N3DSScreenMode N3DS_getScreenMode(void) {
    return gScreenMode;
}

void N3DS_setScreenMode(N3DSScreenMode mode) {
    if ((int) mode < 0 || (int) mode >= (int) N3DS_SCREEN_MODE_COUNT || mode == gScreenMode) return;
    gScreenMode = mode;
    logInfo("Screen mode: %s\n", N3DS_screenModeName(gScreenMode));
}

static bool N3DS_getWindowSize(int32_t* outW, int32_t* outH) {
    *outW = gWindowW;
    *outH = gWindowH;
    return true;
}

static void N3DS_logMemory(const char* when) {
    logInfo("Memory (%s): app free %lu KB of %lu KB, linear free %lu KB, vram free %lu KB\n", when,
        (unsigned long) (osGetMemRegionFree(MEMREGION_APPLICATION) / 1024u),
        (unsigned long) (osGetMemRegionSize(MEMREGION_APPLICATION) / 1024u),
        (unsigned long) (linearSpaceFree() / 1024u),
        (unsigned long) (vramSpaceFree() / 1024u));
}

// 3DS threads start with the VFP in "RunFast" with rounding toward zero (FPSCR 0x03C00000); a PC rounds to nearest
// and keeps denormals. GameMaker games' movement and collision code leans on exact double arithmetic (fractional
// speeds, floor() of positions, pixel-snapping loops), so round toward zero puts Samus a hair off where the game
// expects her (47.9999 instead of 48) and she can slip into or through blocks while moving vertically. The game
// runs on this thread only. Returns the FPSCR found, for the log.
static uint32_t N3DS_useIeeeRounding(void) {
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    uint32_t ieee = fpscr & ~((3u << 22) | (1u << 24)); // round to nearest, no flush-to-zero
    __asm__ volatile("vmsr fpscr, %0" : : "r"(ieee));
    return fpscr;
}

// Sleep mode (closing the lid) and the HOME Menu: aptMainLoop, called once a frame, handles both and blocks until the
// console wakes / the game is back, with the worker threads idle on their events; NDSP stops and restarts its own
// output. On the way back the frame clock starts over, so the first frame's delta_time isn't the time asleep.
static volatile bool gN3DSResumed = false;
static aptHookCookie gN3DSAptCookie;

static void N3DS_aptHook(APT_HookType hook, MAYBE_UNUSED void* param) {
    if (hook == APTHOOK_ONWAKEUP || hook == APTHOOK_ONRESTORE) gN3DSResumed = true;
}

int main(int argc, char** argv) {
    uint32_t startFpscr = N3DS_useIeeeRounding();
    gfxInitDefault();
    romfsInit();
    osSetSpeedupEnable(true);
    APT_SetAppCpuTimeLimit(30);
    aptSetSleepAllowed(true);
    aptHook(&gN3DSAptCookie, N3DS_aptHook, NULL);

    mkdir("sdmc:/3ds", 0777);
    mkdir(N3DS_SD_DIR, 0777);
    N3DSLog_init();
    logInfo("Butterscotch 3DS (%s %s)\n", BUTTERSCOTCH_COMMIT_HASH, BUTTERSCOTCH_COMMIT_DATE);
    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);
    logInfo("Console: %s, speedup %s\n", isNew3DS ? "New 3DS" : "Old 3DS", isNew3DS ? "on" : "n/a");
    {
        uint32_t fpscr;
        __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
        volatile double third = 1.0 / 3.0, tenth = 0.1;
        logInfo("FPU: FPSCR was %08lx, now %08lx (round to nearest); 1/3*3 %s 1, 0.1+0.2 %s 0.3 as on a PC\n",
            (unsigned long) startFpscr, (unsigned long) fpscr, third * 3.0 == 1.0 ? "==" : "!=",
            tenth + 0.2 == 0.3 ? "==" : "!=");
    }
    N3DS_logMemory("startup");

    bool citroReady = false;
    if (C3D_Init(0x80000) && C2D_Init(N3DS_C2D_MAX_OBJECTS)) {
        C2D_Prepare();
        citroReady = true;
    } else {
        C2D_Fini();
        C3D_Fini();
    }

    char* dataWinPath = chooseDataWinPath();
    logInfo("Loading %s\n", dataWinPath);

    N3DSLoadingScreen loadingScreen = {0};
    if (citroReady) {
        loadingScreen.useCitro2D = true;
        loadingScreen.target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        loadingScreen.textBuf = C2D_TextBufNew(512);
        if (loadingScreen.target == NULL || loadingScreen.textBuf == NULL) {
            N3DSLoadingScreen_free(&loadingScreen);
            loadingScreen.useCitro2D = false;
        }
    } else {
        consoleInit(GFX_TOP, &loadingScreen.console);
    }

    // The game's files on the card are installed once (tools/n3ds/make_sd.sh); a build that needs newer ones says so.
    {
        int cardRev = -1;
        FILE* revFile = fopen(N3DS_SD_DIR "sd_data_rev.txt", "r");
        if (revFile != NULL) {
            if (fscanf(revFile, "%d", &cardRev) != 1) cardRev = -1;
            fclose(revFile);
        }
        if (cardRev != N3DS_SD_DATA_REV || !fileExists(dataWinPath)) {
            char message[128];
            if (!fileExists(dataWinPath)) snprintf(message, sizeof(message), "No game files in %s. Press START.", N3DS_SD_DIR);
            else snprintf(message, sizeof(message), "Game files on the SD card are rev %d, this build needs rev %d. Press START.", cardRev, N3DS_SD_DATA_REV);
            logError("%s\n", message);
            N3DS_waitForStartExitScreen(&loadingScreen, message);
            N3DSLoadingScreen_free(&loadingScreen);
            N3DSLog_close();
            return 1;
        }
    }

    N3DSLoadingScreen_set(&loadingScreen, "Scanning data.win", 0, 1);

    DataWinParserOptions options = {0};
    options.parseGen8 = true;
    options.parseOptn = true;
    options.parseLang = true;
    options.parseExtn = true;
    options.parseSond = true;
    options.parseAgrp = true;
    options.parseSprt = true;
    options.parseBgnd = true;
    options.parsePath = true;
    options.parseScpt = true;
    options.parseGlob = true;
    options.parseShdr = true;
    options.parseFont = true;
    options.parseTmln = true;
    options.parseObjt = true;
    options.parseRoom = true;
    options.parseTpag = true;
    options.parseCode = true;
    options.parseVari = true;
    options.parseFunc = true;
    options.parseStrg = true;
    // Textures come from n3ds-preprocess (gfx/), audio is stubbed: TXTR and AUDO stay on the card.
    options.parseTxtr = false;
    options.parseAudo = false;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    options.progressCallback = N3DSDataWinProgressCallback;
    options.progressCallbackUserData = &loadingScreen;
    DataWin* dataWin = DataWin_parse(dataWinPath, options);

    if (dataWin == NULL) {
        logError("Failed to load %s\n", dataWinPath);
        N3DS_waitForStartExitScreen(&loadingScreen, "Failed to load data.win. Press START.");
        N3DSLoadingScreen_free(&loadingScreen);
        N3DSLog_close();
        return 1;
    }
    Gen8* gen8 = &dataWin->gen8;
    logInfo("Loaded \"%s\" [WAD %u / GM %u.%u.%u.%u], %dx%d\n", gen8->name, gen8->wadVersion,
        dataWin->detectedFormat.major, dataWin->detectedFormat.minor, dataWin->detectedFormat.release, dataWin->detectedFormat.build,
        (int) gen8->defaultWindowWidth, (int) gen8->defaultWindowHeight);
    N3DS_logMemory("data.win parsed");

    // Game files next to data.win (romfs or the SD folder), saves in the SD folder: upstream's overlay file system.
    char* dataWinDir = safeStrdup(dataWinPath);
    bsGetDirname(dataWinDir);
    FileSystem* fileSystem = N3DSCachedFileSystem_create((FileSystem*) OverlayFileSystem_create(dataWinDir, N3DS_SD_DIR));
    logInfo("Files: %s (saves %s)\n", dataWinDir, N3DS_SD_DIR);
    free(dataWinDir);
    free(dataWinPath);
#ifdef N3DS_ENABLE_AUDIO
    AudioSystem* audioSystem = (AudioSystem*) N3DSAudioSystem_create(); // initialised by Runner_create
#else
    AudioSystem* audioSystem = (AudioSystem*) NoopAudioSystem_create();
#endif

    N3DSLoadingScreen_set(&loadingScreen, "Initializing renderer", 2, 2);
    // Deleting a screen target unlinks that screen: the loading screen's target must go before the renderer makes its own.
    N3DSLoadingScreen_free(&loadingScreen);
    VMContext* vm = VM_create(dataWin);
    N3DS_preferGameScripts(vm, dataWin);
    Renderer* renderer = N3DSRenderer_create();
    // Input playback runs use a fixed seed, like the desktop runner with --seed 1, so both sides stay in step.
    bool deterministic = fileExists(N3DS_SD_DIR "inputs.json");
    if (deterministic) vm->hasFixedSeed = true;
    Runner* runner = Runner_create(dataWin, vm, renderer, fileSystem, audioSystem, deterministic ? 1u : (uint32_t) osGetTime());

    if (!N3DSRenderer_isReady(renderer)) {
        const char* error = N3DSRenderer_getStartupError(renderer);
        logError("Renderer: %s\n", error != NULL ? error : "init failed");
        consoleInit(GFX_BOTTOM, NULL);
        printf("%s\nPress START to exit.\n", error != NULL ? error : "Renderer init failed");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        return 1;
    }

    N3DSDebugMonitor debugMonitor;
    N3DSDebugMonitor_init(&debugMonitor);

    // The PC version's code paths (input, saves, menus) are the ones AM2R-era games expect.
    runner->osType = OS_WINDOWS;
    runner->setWindowTitle = N3DS_noopSetWindowTitle;
    runner->setWindowSize = N3DS_noopSetWindowSize;
    runner->getWindowSize = N3DS_getWindowSize;
    char** gameArgs = NULL;
    arrput(gameArgs, safeStrdup(argc > 0 && argv[0] != NULL ? argv[0] : "butterscotch"));
    Runner_setGameArgs(runner, gameArgs, (int32_t) arrlen(gameArgs));
    N3DSInput_init(runner);
    N3DSPause_init(runner, renderer);
    N3DSLiveMap_init(runner, renderer);
    N3DSAm2r_init(runner);
    logInfo("Screen mode: %s\n", N3DS_screenModeName(gScreenMode));
    N3DSSavingIndicator savingIndicator = {0};
#ifdef ENABLE_VM_GML_PROFILER
    Profiler_setEnabled(&vm->profiler, true);
#endif
    // Keyboard playback in the desktop --playback-inputs format (e.g. a converted TAS), for regression runs.
    InputRecording* inputPlayback = NULL;
    int32_t inputFrame = 0;
    if (fileExists(N3DS_SD_DIR "inputs.json")) inputPlayback = InputRecording_createPlayer(N3DS_SD_DIR "inputs.json", NULL);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    Runner_initFirstRoom(runner);
    renderer->vtable->flush(renderer);
    C3D_FrameEnd(0);
    N3DSRenderer_collectGarbage(renderer, false);
    N3DS_logMemory("first room");

    bool debugMonitorVisible = true;
    const Room* lastRoom = NULL;
    uint64_t lastFrameStartTime = nowNanos();
    u64 statsStartMs = osGetTime();
    uint32_t statsFrames = 0;
    double statsStepMs = 0.0, statsDrawMs = 0.0, statsWaitMs = 0.0;
    double pacedSpeed = 0.0;
    uint32_t lastUnimplCount = 0;
    u64 lastSlowLogMs = 0;
    double statsMaxFrameMs = 0.0;

    while (aptMainLoop() && !runner->shouldExit) {
        // citro3d paces frames on VBlank at the room speed; a second sleep-based pacer here halved the rate whenever
        // the two drifted out of phase.
        double gameSpeed = Runner_getEffectiveGameSpeed(runner);
        if (gameSpeed <= 0.0 || gameSpeed > 60.0) gameSpeed = 60.0;
        if (gameSpeed != pacedSpeed) {
            C3D_FrameRate((float) gameSpeed);
            pacedSpeed = gameSpeed;
        }

        u64 frameStartTick = svcGetSystemTick();
        memset(gN3DSProfTicks, 0, sizeof(gN3DSProfTicks));
        uint64_t frameStartNow = nowNanos();
        if (gN3DSResumed) {
            gN3DSResumed = false;
            lastFrameStartTime = frameStartNow - (uint64_t) (1.0e9 / gameSpeed);
        }
        runner->deltaTime = (int64_t) (frameStartNow - lastFrameStartTime) / 1000.0;
        lastFrameStartTime = frameStartNow;

        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
        RunnerMouse_beginFrame(runner->mouse);
        N3DSInput_update(runner);
        InputRecording_processFrame(inputPlayback, runner->keyboard, inputFrame++);
        if (N3DSInput_takeMonitorToggle()) debugMonitorVisible = !debugMonitorVisible;

        u64 waitStartTick = svcGetSystemTick();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        double waitMs = (double) (svcGetSystemTick() - waitStartTick) * 1000.0 / (double) SYSCLOCK_ARM11;
        N3DSScreenshot_captureIfRequested(renderer);
        N3DSLiveMap_beginFrame();
        // Before the step: freezes the top screen when the pause screen opens and turns touches into its presses.
        bool paused = N3DSPause_update();
        u64 stepStartTick = svcGetSystemTick();
        Runner_step(runner);
        N3DSAm2r_update();
#ifdef ENABLE_VM_GML_PROFILER
        if (runner->frameCount > 0 && runner->frameCount % 120 == 0) {
            char* report = Profiler_createReport(vm->profiler, 25, 120);
            if (report != NULL) {
                logInfo("%s\n", report);
                free(report);
            }
            Profiler_reset(vm->profiler);
        }
#endif
        float dt = (float) (runner->deltaTime / 1000000.0);
        if (0.0f > dt) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        runner->audioSystem->vtable->update(runner->audioSystem, dt);
        double stepMs = (double) (svcGetSystemTick() - stepStartTick) * 1000.0 / (double) SYSCLOCK_ARM11;

        bool roomChanged = runner->currentRoom != lastRoom;
        if (roomChanged) {
            lastRoom = runner->currentRoom;
            logInfo("Room %d: %s (%dx%d, speed %d)\n", (int) runner->currentRoomIndex, runner->currentRoom->name,
                (int) runner->currentRoom->width, (int) runner->currentRoom->height, (int) runner->currentRoom->speed);
            u64 prewarmStart = N3DSProf_begin();
            N3DSRenderer_prewarmRoom(renderer, runner);
#ifdef N3DS_ENABLE_AUDIO
            N3DSAudio_prewarmRoom(runner->audioSystem, runner);
#endif
            N3DSProf_end(N3DS_PROF_PREWARM, prewarmStart);
            N3DS_logMemory("room start");
        }

        if (!runner->appSurfaceEnabled) {
            runner->applicationWidth = N3DS_TOP_SCREEN_W;
            runner->applicationHeight = N3DS_TOP_SCREEN_H;
            runner->usingAppSurface = false;
        } else {
            if (runner->applicationWidth <= 0 || runner->applicationHeight <= 0) {
                runner->applicationWidth = (int32_t) gen8->defaultWindowWidth;
                runner->applicationHeight = (int32_t) gen8->defaultWindowHeight;
            }
            runner->usingAppSurface = true;
        }
        int32_t gameW = runner->applicationWidth;
        int32_t gameH = runner->applicationHeight;

        // Screen mode (N3DS_DEFAULT_SCREEN_MODE; the touch screen belongs to the pause screen). Wide: upstream's
        // widescreen hack (views grow to the 5:3 top screen, 1:1 pixels). Stretch: the native size scaled to
        // 400x240. Pillarbox: native size 1:1, centred.
        runner->widescreenExtraWidth = 0;
        runner->widescreenExtraHeight = 0;
        if (gScreenMode == N3DS_SCREEN_WIDE && runner->usingAppSurface && gameW > 0 && gameH > 0) {
            int32_t targetW = (int32_t) ((float) gameH * ((float) N3DS_TOP_SCREEN_W / (float) N3DS_TOP_SCREEN_H) + 0.5f);
            if (targetW > gameW) {
                runner->widescreenExtraWidth = targetW - gameW;
                gameW = targetW;
            }
        }
        // The "window" the game sees: the screen, except in Stretch, 1x and 2x where it is the game's own size (and the
        // renderer stretches that to the screen, or scales it by 1 or 2, centred).
        bool stretch = gScreenMode == N3DS_SCREEN_STRETCH;
        int32_t fixedScale = gScreenMode == N3DS_SCREEN_1X ? 1 : gScreenMode == N3DS_SCREEN_2X ? 2 : 0;
        bool gameSizedWindow = stretch || fixedScale > 0;
        gWindowW = gameSizedWindow ? (runner->usingAppSurface ? runner->applicationWidth : (int32_t) gen8->defaultWindowWidth) : N3DS_TOP_SCREEN_W;
        gWindowH = gameSizedWindow ? (runner->usingAppSurface ? runner->applicationHeight : (int32_t) gen8->defaultWindowHeight) : N3DS_TOP_SCREEN_H;
        N3DSRenderer_setStretchToScreen(renderer, stretch);
        N3DSRenderer_setFixedScale(renderer, fixedScale);

        u64 drawStartTick = svcGetSystemTick();
        Runner_drawPre(runner, gWindowW, gWindowH);
        Runner_beginFrame(runner, gameW, gameH, gWindowW, gWindowH, gWindowW, gWindowH);
        Runner_drawViews(runner, gameW, gameH, false);
        renderer->vtable->endFrameInit(renderer);
        Runner_drawPost(runner, gWindowW, gWindowH);
        renderer->vtable->endFrameEnd(renderer);
        Runner_drawGUI(runner, gWindowW, gWindowH, gameW, gameH);
#ifdef N3DS_DIAG_PATTERN
        if (runner->frameCount % 300 == 5) N3DSRenderer_logDiag(renderer);
#endif
        if (paused) N3DSRenderer_drawFrozenTop(renderer);
        if (N3DSPause_inPlay() && N3DSLiveMap_available()) N3DSLiveMap_draw();
        else if (N3DSPause_showMapOnBottom()) N3DSRenderer_drawBottomSnapshot(renderer);
        if (debugMonitorVisible) N3DSDebugMonitor_draw(&debugMonitor, runner, renderer, paused);
        N3DSSavingIndicator_draw(&savingIndicator, renderer, N3DSCachedFileSystem_isSaving(fileSystem));
        renderer->vtable->flush(renderer);
        // After the frame's drawing (all counted as draw): the room change, the renderer's texture collection and the
        // game files' write-back, timed on their own for the slow-frame line.
        u64 roomChangeStartTick = svcGetSystemTick();
        Runner_handlePendingRoomChange(runner);
        renderer->vtable->flush(renderer);
        double roomChangeMs = N3DSProf_ms(svcGetSystemTick() - roomChangeStartTick);
        C3D_FrameEnd(0);
        u64 gcStartTick = svcGetSystemTick();
        N3DSRenderer_collectGarbage(renderer, false);
        double gcMs = N3DSProf_ms(svcGetSystemTick() - gcStartTick);
        u64 fsFlushStartTick = svcGetSystemTick();
        N3DSCachedFileSystem_flush(fileSystem, false);
        double fsFlushMs = N3DSProf_ms(svcGetSystemTick() - fsFlushStartTick);
        u64 frameEndTick = svcGetSystemTick();
        double drawMs = N3DSProf_ms(frameEndTick - drawStartTick);
        double frameTimes[N3DS_FT_COUNT] = {
            [N3DS_FT_FRAME] = N3DSProf_ms(frameEndTick - frameStartTick),
            [N3DS_FT_STEP] = stepMs,
            [N3DS_FT_WAIT] = waitMs,
            [N3DS_FT_DRAW] = drawMs,
            [N3DS_FT_IO] = N3DSProf_ms(gN3DSProfTicks[N3DS_PROF_IO]),
            [N3DS_FT_TEX] = N3DSProf_ms(gN3DSProfTicks[N3DS_PROF_TEX]),
            [N3DS_FT_FS] = N3DSProf_ms(gN3DSProfTicks[N3DS_PROF_FS]),
            [N3DS_FT_AUDIO] = N3DSProf_ms(gN3DSProfTicks[N3DS_PROF_AUDIO]),
            [N3DS_FT_PREWARM] = N3DSProf_ms(gN3DSProfTicks[N3DS_PROF_PREWARM]),
        };
        N3DSDebugMonitor_tickFrame(&debugMonitor, frameTimes);
        if (frameTimes[N3DS_FT_FRAME] > statsMaxFrameMs) statsMaxFrameMs = frameTimes[N3DS_FT_FRAME];
        // A frame over 100 ms is a visible hitch: say where it went (at most twice a second, so a long stall
        // doesn't turn into SD writes of its own).
        u64 frameEndMs = osGetTime();
        if (roomChanged || (frameTimes[N3DS_FT_FRAME] > 100.0 && frameEndMs - lastSlowLogMs >= 500u)) {
            if (!roomChanged) lastSlowLogMs = frameEndMs;
            logInfo("%s %d (%s): %.1f ms = step %.1f + wait %.1f + draw %.1f (room change %.1f, texture gc %.1f, file flush %.1f) + prewarm %.1f; io %.1f, tex %.1f, fs %.1f, audio %.1f\n",
                roomChanged ? "Room frame" : "Slow frame", (int) runner->frameCount,
                runner->currentRoom != NULL ? runner->currentRoom->name : "-",
                frameTimes[N3DS_FT_FRAME], stepMs, waitMs, drawMs, roomChangeMs, gcMs, fsFlushMs, frameTimes[N3DS_FT_PREWARM],
                frameTimes[N3DS_FT_IO], frameTimes[N3DS_FT_TEX], frameTimes[N3DS_FT_FS], frameTimes[N3DS_FT_AUDIO]);
        }

        if (N3DSInput_exitRequested()) {
            FILE* done = fopen(N3DS_SD_DIR "done.txt", "w");
            if (done != NULL) {
                fprintf(done, "frame %d room %s\n", (int) runner->frameCount, runner->currentRoom != NULL ? runner->currentRoom->name : "-");
                fclose(done);
            }
            break;
        }

        N3DSLog_tick();

        statsFrames++;
        statsStepMs += stepMs;
        statsDrawMs += drawMs;
        statsWaitMs += waitMs;
        u64 nowMs = osGetTime();
        if (nowMs - statsStartMs >= 5000u) {
            double seconds = (double) (nowMs - statsStartMs) / 1000.0;
            // The heap reserves the whole app region up front, so report what malloc actually uses.
            struct mallinfo mi = mallinfo();
            uint32_t effects = 0;
#ifdef N3DS_ENABLE_AUDIO
            effects = N3DSAudio_effectsStarted(runner->audioSystem);
#endif
            logInfo("Perf: %.1f fps, step %.2f ms, draw %.2f ms, gpu/vblank wait %.2f ms (avg over %u frames, worst %.0f ms), room %s, heap used %lu KB, linear free %lu KB, sfx %lu\n",
                (double) statsFrames / seconds, statsStepMs / statsFrames, statsDrawMs / statsFrames, statsWaitMs / statsFrames, (unsigned) statsFrames, statsMaxFrameMs,
                runner->currentRoom != NULL ? runner->currentRoom->name : "-",
                (unsigned long) (mi.uordblks / 1024u), (unsigned long) (linearSpaceFree() / 1024u), (unsigned long) effects);
            statsStartMs = nowMs;
            statsFrames = 0;
            statsStepMs = 0.0;
            statsDrawMs = 0.0;
            statsWaitMs = 0.0;
            statsMaxFrameMs = 0.0;
            if (N3DS_unimplCount() != lastUnimplCount) {
                lastUnimplCount = N3DS_unimplCount();
                N3DS_unimplDump("periodic");
            }
        }
    }

    N3DS_unimplDump("exit");
    logInfo("Exiting\n");
    N3DSDebugMonitor_free(&debugMonitor);
    audioSystem->vtable->destroy(audioSystem);
    renderer->vtable->destroy(renderer);
    Runner_free(runner);
    OverlayFileSystem_destroy((OverlayFileSystem*) N3DSCachedFileSystem_destroy(fileSystem));
    VM_free(vm);
    DataWin_free(dataWin);
    N3DSLog_close();
    if (citroReady) {
        C2D_Fini();
        C3D_Fini();
    }
    aptUnhook(&gN3DSAptCookie);
    romfsExit();
    gfxExit();
    return 0;
}
