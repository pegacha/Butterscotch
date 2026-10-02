#include "n3ds_unimpl.h"

#include "../log.h"
#include "../utils.h"

#include <stb/ds/stb_ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char* key;
    uint32_t value;
} N3DSUnimplEntry;

static N3DSUnimplEntry* gUnimpl = NULL;
static uint32_t gUnimplDumped = 0;

void N3DS_unimpl(const char* function, const char* argsFormat, ...) {
    char key[192];
    int n = snprintf(key, sizeof(key), "%s (", function);
    if (n < 0 || n >= (int) sizeof(key)) n = (int) sizeof(key) - 1;
    va_list va;
    va_start(va, argsFormat);
    int m = vsnprintf(key + n, sizeof(key) - (size_t) n, argsFormat, va);
    va_end(va);
    size_t len = strlen(key);
    if (m >= 0 && len + 1 < sizeof(key)) {
        key[len] = ')';
        key[len + 1] = '\0';
    }

    if (gUnimpl == NULL) sh_new_strdup(gUnimpl);
    ptrdiff_t index = shgeti(gUnimpl, key);
    if (index >= 0) {
        gUnimpl[index].value++;
        return;
    }
    shput(gUnimpl, key, 1u);
    logWarn("UNIMPL %s\n", key);
}

void N3DS_unimplDump(const char* reason) {
    uint32_t count = (uint32_t) shlenu(gUnimpl);
    logInfo("UNIMPL summary (%s): %lu unique, %lu new\n", reason, (unsigned long) count, (unsigned long) (count - gUnimplDumped));
    repeat(count, i) {
        logInfo("  UNIMPL %s x%lu\n", gUnimpl[i].key, (unsigned long) gUnimpl[i].value);
    }
    gUnimplDumped = count;
}

uint32_t N3DS_unimplCount(void) {
    return (uint32_t) shlenu(gUnimpl);
}
