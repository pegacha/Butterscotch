// Debug build option N3DS_POISON_MALLOC: new heap memory is filled with a pattern instead of whatever the allocator
// hands back. On hardware, reused heap memory holds old data while an emulator's or a PC's fresh memory is mostly
// zero, so a field the code reads before setting it can behave on one and not the other; this makes the emulator
// behave like the worst case.
#ifdef N3DS_POISON_MALLOC

#include <malloc.h>
#include <stddef.h>
#include <string.h>

#define N3DS_POISON_BYTE 0xA5

void* __real_malloc(size_t size);
void* __real_realloc(void* ptr, size_t size);

void* __wrap_malloc(size_t size) {
    void* p = __real_malloc(size);
    if (p != NULL) memset(p, N3DS_POISON_BYTE, size);
    return p;
}

void* __wrap_realloc(void* ptr, size_t size) {
    size_t old = ptr != NULL ? malloc_usable_size(ptr) : 0;
    void* p = __real_realloc(ptr, size);
    if (p != NULL && size > old) memset((char*) p + old, N3DS_POISON_BYTE, size - old);
    return p;
}

#endif
