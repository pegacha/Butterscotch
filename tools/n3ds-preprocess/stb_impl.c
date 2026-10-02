#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include <stb/image/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/image/stb_image_write.h>

#define STB_DS_IMPLEMENTATION
#include <stb/ds/stb_ds.h>

#include "../../vendor/stb/vorbis/stb_vorbis.c"

#include "../../src/log.h"

void platformLog(const logType type, const char* format, va_list va) {
    FILE* out = type == LOG_TYPE_NORMAL ? stdout : stderr;
    if (type == LOG_TYPE_WARNING) fputs("Warning: ", out);
    if (type == LOG_TYPE_ERROR) fputs("Error: ", out);
    vfprintf(out, format, va);
}
