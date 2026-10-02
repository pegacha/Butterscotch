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

#include "n3ds_file_system.h"
#include "n3ds_input.h"
#include "n3ds_platform_config.h"
#include "n3ds_renderer.h"
#include "n3ds_unimpl.h"

#include <3ds.h>
#include <citro2d.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define N3DS_LOADING_TEXT_SCALE 0.42f
#define N3DS_BOOT_LOG_MAX_LINES 6
#define N3DS_BOOT_LOG_LINE_CHARS 88
#define N3DS_TOTAL_VRAM_BYTES (6u * 1024u * 1024u)
#define N3DS_TOP_SCREEN_W 400
#define N3DS_TOP_SCREEN_H 240

void N3DSLog_init(void);
void N3DSLog_close(void);

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

typedef struct {
    C2D_TextBuf textBuf;
    double displayedFps;
    double displayedRenderMs;
    double sampledRenderMs;
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

static void N3DSDebugMonitor_tickFrame(N3DSDebugMonitor* monitor, double renderMs) {
    if (monitor == NULL) return;

    monitor->sampledFrames++;
    monitor->sampledRenderMs += renderMs;
    u64 nowMs = osGetTime();
    u64 elapsedMs = nowMs - monitor->sampleStartMs;
    if (elapsedMs < 250) return;

    monitor->displayedFps = ((double) monitor->sampledFrames * 1000.0) / (double) elapsedMs;
    monitor->displayedRenderMs = monitor->sampledFrames > 0
        ? monitor->sampledRenderMs / (double) monitor->sampledFrames
        : 0.0;
    monitor->sampledFrames = 0;
    monitor->sampledRenderMs = 0.0;
    monitor->sampleStartMs = nowMs;
}

//font used in godmode9 that's easy to read. got lazy and just embedded the pbm bytes directly here 

static uint8_t N3DSDebugTinyFont_getRow(char c, uint32_t row) {
    static const uint8_t font[95][10] = {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00 },
        { 0x00, 0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A, 0x00, 0x00 },
        { 0x00, 0x04, 0x0E, 0x15, 0x0C, 0x06, 0x15, 0x0E, 0x04, 0x00 },
        { 0x00, 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13, 0x00, 0x00 },
        { 0x00, 0x04, 0x0A, 0x04, 0x09, 0x15, 0x12, 0x0D, 0x00, 0x00 },
        { 0x00, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x02, 0x04, 0x04, 0x04, 0x04, 0x04, 0x02, 0x00, 0x00 },
        { 0x00, 0x08, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08, 0x00, 0x00 },
        { 0x00, 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x18, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00 },
        { 0x00, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x00 },
        { 0x00, 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00, 0x00 },
        { 0x00, 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x1F, 0x01, 0x02, 0x02, 0x04, 0x04, 0x04, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x18, 0x00 },
        { 0x00, 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x13, 0x15, 0x17, 0x10, 0x0F, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x1E, 0x09, 0x09, 0x0E, 0x09, 0x09, 0x1E, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x1E, 0x09, 0x09, 0x09, 0x09, 0x09, 0x1E, 0x00, 0x00 },
        { 0x00, 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00, 0x00 },
        { 0x00, 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00, 0x00 },
        { 0x00, 0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F, 0x00, 0x00 },
        { 0x00, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F, 0x00, 0x00 },
        { 0x00, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x18, 0x00, 0x00 },
        { 0x00, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00, 0x00 },
        { 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00, 0x00 },
        { 0x00, 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00, 0x00 },
        { 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00 },
        { 0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00, 0x00 },
        { 0x00, 0x11, 0x11, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00, 0x00 },
        { 0x00, 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00 },
        { 0x00, 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00, 0x00 },
        { 0x00, 0x06, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x00, 0x00 },
        { 0x00, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x02, 0x02, 0x00 },
        { 0x00, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0C, 0x00, 0x00 },
        { 0x00, 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x00 },
        { 0x00, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00, 0x00 },
        { 0x00, 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0E, 0x11, 0x10, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x01, 0x01, 0x0F, 0x11, 0x11, 0x13, 0x0D, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0F, 0x00, 0x00 },
        { 0x00, 0x06, 0x09, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0D, 0x13, 0x11, 0x11, 0x0F, 0x01, 0x1E },
        { 0x00, 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x1F, 0x00, 0x00 },
        { 0x00, 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x18 },
        { 0x00, 0x10, 0x10, 0x12, 0x14, 0x1C, 0x12, 0x11, 0x00, 0x00 },
        { 0x00, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x1E, 0x10, 0x10 },
        { 0x00, 0x00, 0x00, 0x0F, 0x11, 0x11, 0x13, 0x0D, 0x01, 0x01 },
        { 0x00, 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00, 0x00 },
        { 0x00, 0x04, 0x04, 0x0E, 0x04, 0x04, 0x04, 0x02, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x1E },
        { 0x00, 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00, 0x00 },
        { 0x00, 0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02, 0x00, 0x00 },
        { 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x00, 0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00 },
    };

    if (row >= 10u || c < 32 || c > 126) return 0;
    return font[(uint8_t) c - 32u][row];
}

static void N3DSDebugTinyFont_drawText(const char* text, float x, float y, uint32_t color) {
    if (text == NULL) return;

    const float pixel = 1.0f;
    const float charAdvance = 6.0f;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == ' ') {
            x += charAdvance;
            continue;
        }

        repeat(10u, row) {
            uint8_t bits = N3DSDebugTinyFont_getRow(*cursor, (uint32_t) row);
            repeat(6u, col) {
                if ((bits & (uint8_t) (1u << (5u - col))) == 0u) continue;
                C2D_DrawRectSolid(x + (float) col * pixel, y + (float) row * pixel, 0.0f, pixel, pixel, color);
            }
        }
        x += charAdvance;
    }
}

static void N3DSDebugMonitor_draw(N3DSDebugMonitor* monitor, Runner* runner, Renderer* renderer) {
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
    uint32_t atlasVRAMLimitBytes = N3DSRenderer_getResidentAtlasVRAMLimitBytes(renderer);
    uint32_t atlasPageCount = N3DSRenderer_getResidentAtlasPageCount(renderer);
    uint32_t atlasPageLimit = N3DSRenderer_getResidentAtlasPageLimit(renderer);
    uint32_t directVRAMBytes = N3DSRenderer_getResidentDirectAssetVRAMBytes(renderer);
    uint32_t trackedVRAMBytes = atlasVRAMBytes + directVRAMBytes;
    uint32_t ramFreeBytes = osGetMemRegionFree(MEMREGION_APPLICATION);
    uint32_t ramTotalBytes = osGetMemRegionSize(MEMREGION_APPLICATION);
    uint32_t linearFreeBytes = linearSpaceFree();

    char fpsLine[96];
    char vramLine[64];
    char atlasLine[96];
    char ramLine[96];
    char audioLine[96];
    char roomLine[96];
    char vramUsed[24];
    char vramTotal[24];
    char atlasUsed[24];
    char atlasLimit[24];
    char directUsed[24];
    char ramFree[24];
    char ramTotal[24];
    char linearFree[24];
    char audioCached[24];
    char audioLimit[24];
    uint32_t cachedSounds = 0;
    uint32_t totalSounds = 0;
    uint32_t cachedSoundBytes = 0;
    uint32_t cacheLimitBytes = 0;

    char unimplLine[64];
    snprintf(unimplLine, sizeof(unimplLine), "UNIMPL %lu  F %d", (unsigned long) N3DS_unimplCount(), (int) runner->frameCount);

    snprintf(
        fpsLine,
        sizeof(fpsLine),
        "FPS %.1f  R %.1fms",
        monitor->displayedFps > 0.0 ? monitor->displayedFps : 0.0,
        monitor->displayedRenderMs > 0.0 ? monitor->displayedRenderMs : 0.0
    );
    N3DS_formatDebugSize(vramUsed, sizeof(vramUsed), trackedVRAMBytes);
    N3DS_formatDebugSize(vramTotal, sizeof(vramTotal), N3DS_TOTAL_VRAM_BYTES);
    N3DS_formatDebugSize(atlasUsed, sizeof(atlasUsed), atlasVRAMBytes);
    N3DS_formatDebugSize(atlasLimit, sizeof(atlasLimit), atlasVRAMLimitBytes);
    N3DS_formatDebugSize(directUsed, sizeof(directUsed), directVRAMBytes);
    N3DS_formatDebugSize(ramFree, sizeof(ramFree), ramFreeBytes);
    N3DS_formatDebugSize(ramTotal, sizeof(ramTotal), ramTotalBytes);
    N3DS_formatDebugSize(linearFree, sizeof(linearFree), linearFreeBytes);
    N3DS_formatDebugSize(audioCached, sizeof(audioCached), cachedSoundBytes);
    N3DS_formatDebugSize(audioLimit, sizeof(audioLimit), cacheLimitBytes);
    snprintf(vramLine, sizeof(vramLine), "VRAM %s/%s",
        vramUsed,
        vramTotal);
    snprintf(atlasLine, sizeof(atlasLine), "AT %lu/%lu %s  DR %s",
        (unsigned long) atlasPageCount,
        (unsigned long) atlasPageLimit,
        atlasUsed,
        directUsed);
    snprintf(ramLine, sizeof(ramLine), "RAM %s/%s  L %s",
        ramFree,
        ramTotal,
        linearFree);
    snprintf(audioLine, sizeof(audioLine), "AUD %lu/%lu %s/%s",
        (unsigned long) cachedSounds,
        (unsigned long) totalSounds,
        audioCached,
        audioLimit);
    snprintf(roomLine, sizeof(roomLine), "R %.28s", roomName);

    N3DSRenderer_beginBottomScreenGUI(renderer, 320, 240);
    const float boxX = 92.0f;
    const float boxY = 8.0f;
    const float boxW = 220.0f;
    const float boxH = 110.0f;
    const float textX = boxX + 7.0f;
    C2D_DrawRectSolid(boxX, boxY, 0.0f, boxW, boxH, C2D_Color32(8, 10, 16, 218));
    C2D_DrawRectSolid(boxX, boxY, 0.0f, boxW, 2.0f, C2D_Color32(77, 118, 255, 245));
    C2D_DrawRectSolid(boxX, boxY + boxH - 1.0f, 0.0f, boxW, 1.0f, C2D_Color32(32, 46, 78, 230));
    N3DSDebugTinyFont_drawText("DBG", textX, boxY + 7.0f, C2D_Color32(255, 255, 255, 255));
    N3DSDebugTinyFont_drawText(fpsLine, boxX + 42.0f, boxY + 7.0f, C2D_Color32(196, 230, 255, 255));
    N3DSDebugTinyFont_drawText(vramLine, textX, boxY + 25.0f, C2D_Color32(196, 230, 255, 255));
    N3DSDebugTinyFont_drawText(atlasLine, textX, boxY + 42.0f, C2D_Color32(146, 188, 236, 255));
    N3DSDebugTinyFont_drawText(ramLine, textX, boxY + 59.0f, C2D_Color32(196, 230, 255, 255));
    N3DSDebugTinyFont_drawText(unimplLine, textX, boxY + 76.0f, C2D_Color32(255, 196, 196, 255));
    N3DSDebugTinyFont_drawText(roomLine, textX, boxY + 93.0f, C2D_Color32(255, 230, 163, 255));
    N3DSRenderer_endBottomScreenGUI(renderer);
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

static s64 N3DS_ticksToNs(u64 ticks) {
    return (s64) ((ticks * 1000000000ULL) / SYSCLOCK_ARM11);
}

static void N3DS_sleepUntilTick(u64 targetTick) {
    const u64 coarseGuardTicks = SYSCLOCK_ARM11 / 2000u; // ~0.5 ms

    while (true) {
        u64 now = svcGetSystemTick();
        if (now >= targetTick) return;

        u64 remaining = targetTick - now;
        if (remaining <= coarseGuardTicks) break;

        svcSleepThread(N3DS_ticksToNs(remaining - coarseGuardTicks));
    }

    while (svcGetSystemTick() < targetTick) {
    }
}

static void N3DS_beginPacedFrame(u64* nextFrameTick, u64 frameTicks) {
    if (nextFrameTick == NULL || frameTicks == 0) return;

    u64 now = svcGetSystemTick();
    if (*nextFrameTick == 0) {
        *nextFrameTick = now;
    } else if (now > *nextFrameTick + frameTicks * 4u) {
        *nextFrameTick = now;
    } else {
        while (now > *nextFrameTick) {
            *nextFrameTick += frameTicks;
        }
    }

    N3DS_sleepUntilTick(*nextFrameTick);
    *nextFrameTick += frameTicks;
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
static bool N3DS_getWindowSize(int32_t* outW, int32_t* outH) {
    *outW = N3DS_TOP_SCREEN_W;
    *outH = N3DS_TOP_SCREEN_H;
    return true;
}

static void N3DS_logMemory(const char* when) {
    logInfo("Memory (%s): app free %lu KB of %lu KB, linear free %lu KB, vram free %lu KB\n", when,
        (unsigned long) (osGetMemRegionFree(MEMREGION_APPLICATION) / 1024u),
        (unsigned long) (osGetMemRegionSize(MEMREGION_APPLICATION) / 1024u),
        (unsigned long) (linearSpaceFree() / 1024u),
        (unsigned long) (vramSpaceFree() / 1024u));
}

int main(int argc, char** argv) {
    gfxInitDefault();
    romfsInit();
    osSetSpeedupEnable(true);
    APT_SetAppCpuTimeLimit(30);

    mkdir("sdmc:/3ds", 0777);
    mkdir(N3DS_SD_DIR, 0777);
    N3DSLog_init();
    logInfo("Butterscotch 3DS (%s %s)\n", BUTTERSCOTCH_COMMIT_HASH, BUTTERSCOTCH_COMMIT_DATE);
    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);
    logInfo("Console: %s, speedup %s\n", isNew3DS ? "New 3DS" : "Old 3DS", isNew3DS ? "on" : "n/a");
    N3DS_logMemory("startup");

    bool citroReady = false;
    if (C3D_Init(0x80000) && C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
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

    char* dataWinDir = safeStrdup(dataWinPath);
    bsGetDirname(dataWinDir);
    FileSystem* fileSystem = (FileSystem*) N3DSFileSystem_create("romfs:/", N3DS_SD_DIR);
    free(dataWinDir);
    free(dataWinPath);
    AudioSystem* audioSystem = (AudioSystem*) NoopAudioSystem_create();

    N3DSLoadingScreen_set(&loadingScreen, "Initializing renderer", 2, 2);
    // Deleting a screen target unlinks that screen: the loading screen's target must go before the renderer makes its own.
    N3DSLoadingScreen_free(&loadingScreen);
    VMContext* vm = VM_create(dataWin);
    Renderer* renderer = N3DSRenderer_create();
    Runner* runner = Runner_create(dataWin, vm, renderer, fileSystem, audioSystem, (uint32_t) osGetTime());

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

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    Runner_initFirstRoom(runner);
    renderer->vtable->flush(renderer);
    C3D_FrameEnd(0);
    N3DSRenderer_collectGarbage(renderer, false);
    N3DS_logMemory("first room");

    bool debugMonitorVisible = true;
    const Room* lastRoom = NULL;
    uint64_t lastFrameStartTime = nowNanos();
    u64 nextFrameTick = 0;
    u64 statsStartMs = osGetTime();
    uint32_t statsFrames = 0;
    double statsStepMs = 0.0, statsDrawMs = 0.0;
    uint32_t lastUnimplCount = 0;

    while (aptMainLoop() && !runner->shouldExit) {
        double gameSpeed = Runner_getEffectiveGameSpeed(runner);
        if (gameSpeed <= 0.0) gameSpeed = 60.0;
        N3DS_beginPacedFrame(&nextFrameTick, (u64) ((double) SYSCLOCK_ARM11 / gameSpeed));

        uint64_t frameStartNow = nowNanos();
        runner->deltaTime = (int64_t) (frameStartNow - lastFrameStartTime) / 1000.0;
        lastFrameStartTime = frameStartNow;

        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
        RunnerMouse_beginFrame(runner->mouse);
        u32 down = N3DSInput_update(runner);
        if ((down & KEY_START) && (hidKeysHeld() & KEY_SELECT)) break;
        if ((down & KEY_SELECT) && (hidKeysHeld() & KEY_L)) debugMonitorVisible = !debugMonitorVisible;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        N3DSScreenshot_captureIfRequested(renderer);
        u64 stepStartTick = svcGetSystemTick();
        Runner_step(runner);
        float dt = (float) (runner->deltaTime / 1000000.0);
        if (0.0f > dt) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        runner->audioSystem->vtable->update(runner->audioSystem, dt);
        double stepMs = (double) (svcGetSystemTick() - stepStartTick) * 1000.0 / (double) SYSCLOCK_ARM11;

        if (runner->currentRoom != lastRoom) {
            lastRoom = runner->currentRoom;
            logInfo("Room %d: %s (%dx%d, speed %d)\n", (int) runner->currentRoomIndex, runner->currentRoom->name,
                (int) runner->currentRoom->width, (int) runner->currentRoom->height, (int) runner->currentRoom->speed);
            N3DSRenderer_prewarmRoom(renderer, runner);
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

        u64 drawStartTick = svcGetSystemTick();
        Runner_drawPre(runner, N3DS_TOP_SCREEN_W, N3DS_TOP_SCREEN_H);
        Runner_beginFrame(runner, gameW, gameH, N3DS_TOP_SCREEN_W, N3DS_TOP_SCREEN_H, N3DS_TOP_SCREEN_W, N3DS_TOP_SCREEN_H);
        Runner_drawViews(runner, gameW, gameH, false);
        renderer->vtable->endFrameInit(renderer);
        Runner_drawPost(runner, N3DS_TOP_SCREEN_W, N3DS_TOP_SCREEN_H);
        renderer->vtable->endFrameEnd(renderer);
        Runner_drawGUI(runner, N3DS_TOP_SCREEN_W, N3DS_TOP_SCREEN_H, gameW, gameH);
        if (runner->frameCount % 300 == 5) N3DSRenderer_logDiag(renderer);
        if (debugMonitorVisible) N3DSDebugMonitor_draw(&debugMonitor, runner, renderer);
        renderer->vtable->flush(renderer);
        Runner_handlePendingRoomChange(runner);
        renderer->vtable->flush(renderer);
        C3D_FrameEnd(0);
        N3DSRenderer_collectGarbage(renderer, false);
        double drawMs = (double) (svcGetSystemTick() - drawStartTick) * 1000.0 / (double) SYSCLOCK_ARM11;
        N3DSDebugMonitor_tickFrame(&debugMonitor, drawMs);

        if (N3DSInput_exitRequested()) {
            FILE* done = fopen(N3DS_SD_DIR "done.txt", "w");
            if (done != NULL) {
                fprintf(done, "frame %d room %s\n", (int) runner->frameCount, runner->currentRoom != NULL ? runner->currentRoom->name : "-");
                fclose(done);
            }
            break;
        }

        statsFrames++;
        statsStepMs += stepMs;
        statsDrawMs += drawMs;
        u64 nowMs = osGetTime();
        if (nowMs - statsStartMs >= 5000u) {
            double seconds = (double) (nowMs - statsStartMs) / 1000.0;
            logInfo("Perf: %.1f fps, step %.2f ms, draw %.2f ms (avg over %u frames), room %s, app free %lu KB, linear free %lu KB\n",
                (double) statsFrames / seconds, statsStepMs / statsFrames, statsDrawMs / statsFrames, (unsigned) statsFrames,
                runner->currentRoom != NULL ? runner->currentRoom->name : "-",
                (unsigned long) (osGetMemRegionFree(MEMREGION_APPLICATION) / 1024u), (unsigned long) (linearSpaceFree() / 1024u));
            statsStartMs = nowMs;
            statsFrames = 0;
            statsStepMs = 0.0;
            statsDrawMs = 0.0;
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
    N3DSFileSystem_destroy((N3DSFileSystem*) fileSystem);
    VM_free(vm);
    DataWin_free(dataWin);
    N3DSLog_close();
    if (citroReady) {
        C2D_Fini();
        C3D_Fini();
    }
    romfsExit();
    gfxExit();
    return 0;
}
