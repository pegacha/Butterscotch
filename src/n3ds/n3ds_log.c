#include "../log.h"
#include "n3ds_platform_config.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

// platformLog for the 3DS: N3DS_SD_DIR "log.txt", each line stamped with seconds since start.
// Also sent to svcOutputDebugString (shown by Azahar / GDB).

static FILE* gLogFile = NULL;
static u64 gLogStartMs = 0;
static bool gAtLineStart = true;

void N3DSLog_init(void) {
    gLogStartMs = osGetTime();
    gLogFile = fopen(N3DS_SD_DIR "log.txt", "w");
}

void N3DSLog_close(void) {
    if (gLogFile != NULL) fclose(gLogFile);
    gLogFile = NULL;
}

void platformLog(const logType type, const char* format, va_list va) {
    char buffer[1024];
    const char* prefix = "";
    switch (type) {
        case LOG_TYPE_WARNING: prefix = "Warning: "; break;
        case LOG_TYPE_ERROR: prefix = "Error: "; break;
        case LOG_TYPE_DEBUG: prefix = "Debug: "; break;
        default: break;
    }
    int written = snprintf(buffer, sizeof(buffer), "%s", prefix);
    vsnprintf(buffer + written, sizeof(buffer) - (size_t) written, format, va);
    buffer[sizeof(buffer) - 1] = '\0';

    svcOutputDebugString(buffer, (s32) strlen(buffer));
    if (gLogFile == NULL) return;
    if (gAtLineStart) fprintf(gLogFile, "[%8.3f] ", (double) (osGetTime() - gLogStartMs) / 1000.0);
    fputs(buffer, gLogFile);
    size_t len = strlen(buffer);
    gAtLineStart = len > 0 && buffer[len - 1] == '\n';
    fflush(gLogFile);
}
