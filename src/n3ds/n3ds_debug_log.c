#include "n3ds_debug_log.h"

#if N3DS_DEBUG_BREADCRUMB_LOGGING

#include <3ds.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define N3DS_DEBUG_HISTORY_LOG_PATH "sdmc:/3ds/cinnamon/crash_history.log"
#define N3DS_DEBUG_MARKER_LOG_PATH "sdmc:/3ds/cinnamon/last_breadcrumb.txt"
#define N3DS_DEBUG_MARKER_BUFFER_SIZE 256

static FILE* gN3DSDebugHistoryLog = NULL;
static FILE* gN3DSDebugMarkerLog = NULL;
static u64 gN3DSDebugStartTick = 0;
static uint32_t gN3DSDebugEventCounter = 0;
static char gN3DSDebugLastMarker[N3DS_DEBUG_MARKER_BUFFER_SIZE];

static double N3DSDebugLog_ticksToMs(u64 ticks) {
    return (double) ticks * 1000.0 / (double) SYSCLOCK_ARM11;
}

static void N3DSDebugLog_openIfNeeded(void) {
    if (gN3DSDebugStartTick == 0) gN3DSDebugStartTick = svcGetSystemTick();

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/cinnamon", 0777);

    if (gN3DSDebugHistoryLog == NULL) {
        gN3DSDebugHistoryLog = fopen(N3DS_DEBUG_HISTORY_LOG_PATH, "a");
        if (gN3DSDebugHistoryLog != NULL) {
            setvbuf(gN3DSDebugHistoryLog, NULL, _IOLBF, 0);
        }
    }

    if (gN3DSDebugMarkerLog == NULL) {
        gN3DSDebugMarkerLog = fopen(N3DS_DEBUG_MARKER_LOG_PATH, "w");
        if (gN3DSDebugMarkerLog != NULL) {
            setvbuf(gN3DSDebugMarkerLog, NULL, _IONBF, 0);
        }
    }
}

void N3DSDebugLog_init(void) {
    N3DSDebugLog_openIfNeeded();
    N3DSDebugLog_event("boot", "debug log initialized");
}

void N3DSDebugLog_close(void) {
    if (gN3DSDebugMarkerLog != NULL) {
        fclose(gN3DSDebugMarkerLog);
        gN3DSDebugMarkerLog = NULL;
    }
    if (gN3DSDebugHistoryLog != NULL) {
        fclose(gN3DSDebugHistoryLog);
        gN3DSDebugHistoryLog = NULL;
    }
}

void N3DSDebugLog_event(const char* tag, const char* fmt, ...) {
    N3DSDebugLog_openIfNeeded();
    if (gN3DSDebugHistoryLog == NULL) return;

    char message[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt != NULL ? fmt : "", args);
    va_end(args);

    double elapsedMs = N3DSDebugLog_ticksToMs(svcGetSystemTick() - gN3DSDebugStartTick);
    fprintf(
        gN3DSDebugHistoryLog,
        "[%10.2f ms] #%06lu %-10s %s\n",
        elapsedMs,
        (unsigned long) ++gN3DSDebugEventCounter,
        tag != NULL ? tag : "event",
        message
    );
    fflush(gN3DSDebugHistoryLog);
}

void N3DSDebugLog_setMarker(
    const char* stage,
    uint32_t frame,
    const char* roomName,
    int32_t detailA,
    int32_t detailB
) {
    N3DSDebugLog_openIfNeeded();
    if (gN3DSDebugMarkerLog == NULL) return;

    char line[N3DS_DEBUG_MARKER_BUFFER_SIZE];
    double elapsedMs = N3DSDebugLog_ticksToMs(svcGetSystemTick() - gN3DSDebugStartTick);
    snprintf(
        line,
        sizeof(line),
        "t=%10.2fms frame=%lu stage=%s room=%s a=%ld b=%ld",
        elapsedMs,
        (unsigned long) frame,
        stage != NULL ? stage : "<null>",
        roomName != NULL ? roomName : "(none)",
        (long) detailA,
        (long) detailB
    );

    if (strncmp(line, gN3DSDebugLastMarker, sizeof(gN3DSDebugLastMarker)) == 0) {
        return;
    }
    snprintf(gN3DSDebugLastMarker, sizeof(gN3DSDebugLastMarker), "%s", line);

    rewind(gN3DSDebugMarkerLog);
    fprintf(gN3DSDebugMarkerLog, "%-240s\n", line);
    fflush(gN3DSDebugMarkerLog);
}

#endif
