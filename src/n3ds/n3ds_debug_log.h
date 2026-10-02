#pragma once

#include <stdint.h>

#ifndef N3DS_DEBUG_BREADCRUMB_LOGGING
#define N3DS_DEBUG_BREADCRUMB_LOGGING 0
#endif

#if N3DS_DEBUG_BREADCRUMB_LOGGING
void N3DSDebugLog_init(void);
void N3DSDebugLog_close(void);
void N3DSDebugLog_event(const char* tag, const char* fmt, ...);
void N3DSDebugLog_setMarker(
    const char* stage,
    uint32_t frame,
    const char* roomName,
    int32_t detailA,
    int32_t detailB
);
#else
#define N3DSDebugLog_init() ((void) 0)
#define N3DSDebugLog_close() ((void) 0)
#define N3DSDebugLog_event(...) ((void) 0)
#define N3DSDebugLog_setMarker(...) ((void) 0)
#endif
