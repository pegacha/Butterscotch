#include "n3ds_cached_file_system.h"

#include "../utils.h"
#include "n3ds_prof.h"

#include <ctype.h>
#include <stb/ds/stb_ds.h>
#include <stdlib.h>
#include <string.h>

// The game's files on the SD card, with the card kept out of the frame loop:
//  - file_exists/directory_exists answers are cached (each is a real SD stat, milliseconds; AM2R checks for a temp
//    screenshot file every frame).
//  - Files the game reads or writes are held in memory and written back once they have been left alone for a
//    second (N3DSCachedFileSystem_flush), and only if they differ from what the card holds. AM2R decrypts its save
//    in place, reads it, and encrypts it again (a 236 KB file written twice per save read, and the save-select
//    screen reads every slot); held in memory that is no SD write at all.
//  - Binary files (file_bin_*) are memory buffers between open and close: AM2R's crypt script works a byte at a
//    time with a seek per byte, and through stdio each seek dropped the buffer and went to the card.
// This file system is the only thing writing to the game folder while the game runs.

#define N3DS_FS_WRITE_BACK_DELAY_MS 1000u
// ...or once it has been waiting this long, for a file the game keeps rewriting.
#define N3DS_FS_WRITE_BACK_MAX_DELAY_MS 5000u
// Files read and left unchanged are kept for later reads up to this size each and this much in total.
#define N3DS_FS_CLEAN_FILE_MAX_BYTES (1024 * 1024)
#define N3DS_FS_CLEAN_TOTAL_MAX_BYTES (4 * 1024 * 1024)

typedef struct {
    char* key;
    bool value;
} N3DSExistsEntry;

typedef struct {
    char* path;       // as the game first named it (the key is normalised)
    uint8_t* data;    // what the game sees; the same pointer as disk while unchanged
    int32_t size;
    bool exists;
    uint8_t* disk;    // what the card holds
    int32_t diskSize;
    bool diskExists;
    u64 changedMs;
    u64 dirtySinceMs;
} N3DSFileEntry;

typedef struct {
    char* key;
    N3DSFileEntry value;
} N3DSFileMapEntry;

typedef struct {
    FileSystem base;
    FileSystem* inner;
    N3DSExistsEntry* files;
    N3DSExistsEntry* dirs;
    N3DSFileMapEntry* contents;
} N3DSCachedFileSystem;

#define INNER(fs) (((N3DSCachedFileSystem*) (fs))->inner)
// Evaluates a call on the inner file system and counts its time as game file I/O.
#define TIMED(expr) ({ u64 profStart_ = N3DSProf_begin(); __typeof__(expr) r_ = (expr); N3DSProf_end(N3DS_PROF_FS, profStart_); r_; })

static void invalidate(FileSystem* fs) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    shfree(c->files);
    shfree(c->dirs);
    sh_new_strdup(c->files);
    sh_new_strdup(c->dirs);
}

// ===[ File contents ]===

// FAT is case-insensitive and the game spells paths in more than one way.
static void normaliseKey(char* out, size_t outSize, const char* path) {
    size_t n = 0;
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    for (const char* s = path; *s != '\0' && n + 1 < outSize; s++) {
        char ch = *s == '\\' ? '/' : (char) tolower((unsigned char) *s);
        if (ch == '/' && n > 0 && out[n - 1] == '/') continue;
        out[n++] = ch;
    }
    out[n] = '\0';
}

static bool entryDirty(const N3DSFileEntry* e) {
    if (e->exists != e->diskExists) return true;
    if (!e->exists || e->data == e->disk) return false;
    return e->size != e->diskSize || memcmp(e->data, e->disk, (size_t) e->size) != 0;
}

static void freeEntry(N3DSFileEntry* e) {
    if (e->data != e->disk) free(e->data);
    free(e->disk);
    free(e->path);
}

// The entry for a path, read from the card the first time.
static N3DSFileEntry* loadEntry(N3DSCachedFileSystem* c, const char* path) {
    char key[512];
    normaliseKey(key, sizeof(key), path);
    ptrdiff_t i = shgeti(c->contents, key);
    if (i >= 0) return &c->contents[i].value;

    N3DSFileEntry e = {0};
    e.path = safeStrdup(path);
    uint8_t* data = NULL;
    int32_t size = 0;
    if (TIMED(c->inner->vtable->readFileBinary(c->inner, path, &data, &size))) {
        e.disk = data;
        e.diskSize = size;
        e.diskExists = true;
    }
    e.data = e.disk;
    e.size = e.diskSize;
    e.exists = e.diskExists;
    shput(c->contents, key, e);
    return &c->contents[shgeti(c->contents, key)].value;
}

// Replaces what the game sees (takes ownership of data).
static void setEntry(N3DSFileEntry* e, uint8_t* data, int32_t size, bool exists, u64 nowMs) {
    if (!entryDirty(e)) e->dirtySinceMs = nowMs;
    if (e->data != e->disk) free(e->data);
    e->data = data;
    e->size = exists ? size : 0;
    e->exists = exists;
    e->changedMs = nowMs;
    // Back to what the card holds (the decrypt/encrypt round trip): drop the copy, nothing to write.
    if (!entryDirty(e)) {
        if (e->data != e->disk) free(e->data);
        e->data = e->disk;
        e->size = e->diskSize;
    }
}

static uint8_t* dupBytes(const void* src, int32_t size) {
    uint8_t* copy = safeMalloc((size_t) (size > 0 ? size : 1));
    if (size > 0) memcpy(copy, src, (size_t) size);
    return copy;
}

static void writeBack(N3DSCachedFileSystem* c, N3DSFileEntry* e) {
    if (!entryDirty(e)) return;
    bool ok;
    if (e->exists) {
        ok = TIMED(c->inner->vtable->writeFileBinary(c->inner, e->path, e->data != NULL ? e->data : (const uint8_t*) "", e->size));
    } else {
        ok = TIMED(c->inner->vtable->deleteFile(c->inner, e->path)) || !e->diskExists;
    }
    invalidate(&c->base);
    if (!ok) return;
    if (e->disk != e->data) free(e->disk);
    e->disk = e->data;
    e->diskSize = e->size;
    e->diskExists = e->exists;
}

void N3DSCachedFileSystem_flush(FileSystem* fs, bool force) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    u64 nowMs = osGetTime();
    int32_t cleanBytes = 0;
    for (ptrdiff_t i = 0; i < shlen(c->contents); i++) {
        N3DSFileEntry* e = &c->contents[i].value;
        if (entryDirty(e)) {
            if (force || nowMs - e->changedMs >= N3DS_FS_WRITE_BACK_DELAY_MS || nowMs - e->dirtySinceMs >= N3DS_FS_WRITE_BACK_MAX_DELAY_MS) writeBack(c, e);
        } else {
            cleanBytes += e->size;
        }
    }
    if (cleanBytes <= N3DS_FS_CLEAN_TOTAL_MAX_BYTES) return;
    // Too much kept for reads: drop every unchanged file (they are read again from the card when needed).
    for (ptrdiff_t i = shlen(c->contents) - 1; i >= 0; i--) {
        if (entryDirty(&c->contents[i].value)) continue;
        freeEntry(&c->contents[i].value);
        shdel(c->contents, c->contents[i].key);
    }
    invalidate(fs);
}

// A large file only read is not worth keeping a copy of.
static void dropIfLargeAndClean(N3DSCachedFileSystem* c, const char* path) {
    char key[512];
    normaliseKey(key, sizeof(key), path);
    ptrdiff_t i = shgeti(c->contents, key);
    if (i < 0) return;
    N3DSFileEntry* e = &c->contents[i].value;
    if (entryDirty(e) || N3DS_FS_CLEAN_FILE_MAX_BYTES >= e->size) return;
    freeEntry(e);
    shdel(c->contents, key);
}

static void flushPath(N3DSCachedFileSystem* c, const char* path) {
    char key[512];
    normaliseKey(key, sizeof(key), path);
    ptrdiff_t i = shgeti(c->contents, key);
    if (i >= 0) writeBack(c, &c->contents[i].value);
}

// ===[ Whole-file calls ]===

// Whoever asks for the real path is going to the card itself: put the file there first.
static char* cResolvePath(FileSystem* fs, const char* p) {
    flushPath((N3DSCachedFileSystem*) fs, p);
    return INNER(fs)->vtable->resolvePath(INNER(fs), p);
}

static bool cFileExists(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    char key[512];
    normaliseKey(key, sizeof(key), p);
    ptrdiff_t k = shgeti(c->contents, key);
    if (k >= 0) return c->contents[k].value.exists;
    ptrdiff_t i = shgeti(c->files, p);
    if (i >= 0) return c->files[i].value;
    bool exists = TIMED(c->inner->vtable->fileExists(c->inner, p));
    shput(c->files, p, exists);
    return exists;
}

static char* cReadFileText(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    N3DSFileEntry* e = loadEntry(c, p);
    if (!e->exists) return NULL;
    char* text = safeMalloc((size_t) e->size + 1);
    if (e->size > 0) memcpy(text, e->data, (size_t) e->size);
    text[e->size] = '\0';
    dropIfLargeAndClean(c, p);
    return text;
}

static bool cReadFileBinary(FileSystem* fs, const char* p, uint8_t** d, int32_t* n) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    N3DSFileEntry* e = loadEntry(c, p);
    if (!e->exists) return false;
    *d = dupBytes(e->data, e->size);
    *n = e->size;
    dropIfLargeAndClean(c, p);
    return true;
}

static bool cWriteFileText(FileSystem* fs, const char* p, const char* s) {
    int32_t size = (int32_t) strlen(s);
    setEntry(loadEntry((N3DSCachedFileSystem*) fs, p), dupBytes(s, size), size, true, osGetTime());
    return true;
}

static bool cWriteFileBinary(FileSystem* fs, const char* p, const uint8_t* d, int32_t n) {
    setEntry(loadEntry((N3DSCachedFileSystem*) fs, p), dupBytes(d, n), n, true, osGetTime());
    return true;
}

static bool cDeleteFile(FileSystem* fs, const char* p) {
    N3DSFileEntry* e = loadEntry((N3DSCachedFileSystem*) fs, p);
    if (!e->exists) return false;
    setEntry(e, NULL, 0, false, osGetTime());
    return true;
}

static bool cRenameFile(FileSystem* fs, const char* a, const char* b) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    N3DSFileEntry* from = loadEntry(c, a);
    if (!from->exists) return false;
    uint8_t* data = dupBytes(from->data, from->size);
    int32_t size = from->size;
    u64 nowMs = osGetTime();
    // loadEntry can grow the map, so look the source up again afterwards.
    setEntry(loadEntry(c, b), data, size, true, nowMs);
    setEntry(loadEntry(c, a), NULL, 0, false, nowMs);
    return true;
}

// ===[ Binary files ]===

typedef struct {
    char* path;
    uint8_t* data;
    int32_t size;
    int32_t capacity;
    int32_t pos;
    bool dirty;
} N3DSMemFile;

static void memFileReserve(N3DSMemFile* f, int32_t needed) {
    if (needed <= f->capacity) return;
    int32_t capacity = f->capacity > 0 ? f->capacity : 256;
    while (capacity < needed) capacity *= 2;
    f->data = safeRealloc(f->data, (size_t) capacity);
    f->capacity = capacity;
}

static void* cBinaryOpen(FileSystem* fs, const char* p, int32_t mode) {
    N3DSFileEntry* e = loadEntry((N3DSCachedFileSystem*) fs, p);
    if (mode == GML_FILE_BIN_READ && !e->exists) return NULL;
    bool keep = mode != GML_FILE_BIN_WRITE && e->exists;
    N3DSMemFile* f = safeCalloc(1, sizeof(N3DSMemFile));
    f->path = safeStrdup(p);
    f->size = keep ? e->size : 0;
    f->capacity = f->size;
    f->data = keep ? dupBytes(e->data, e->size) : NULL;
    // Opening for writing creates (or truncates) the file, as fopen would.
    f->dirty = !keep;
    return f;
}

static void cBinaryClose(FileSystem* fs, void* h) {
    N3DSMemFile* f = h;
    if (f == NULL) return;
    if (f->dirty) {
        setEntry(loadEntry((N3DSCachedFileSystem*) fs, f->path), f->data, f->size, true, osGetTime());
        f->data = NULL;
    }
    free(f->data);
    free(f->path);
    free(f);
}

static int32_t cBinaryRead(MAYBE_UNUSED FileSystem* fs, void* h, void* d, int32_t n) {
    N3DSMemFile* f = h;
    if (f == NULL || 0 >= n || f->pos >= f->size) return 0;
    if (n > f->size - f->pos) n = f->size - f->pos;
    memcpy(d, f->data + f->pos, (size_t) n);
    f->pos += n;
    return n;
}

static int32_t cBinaryWrite(MAYBE_UNUSED FileSystem* fs, void* h, const void* s, int32_t n) {
    N3DSMemFile* f = h;
    if (f == NULL || 0 >= n) return 0;
    memFileReserve(f, f->pos + n);
    // Writing past the end (after a seek beyond it) fills the gap with zeros, as stdio does.
    if (f->pos > f->size) memset(f->data + f->size, 0, (size_t) (f->pos - f->size));
    memcpy(f->data + f->pos, s, (size_t) n);
    f->pos += n;
    if (f->pos > f->size) f->size = f->pos;
    f->dirty = true;
    return n;
}

static int32_t cBinaryTell(MAYBE_UNUSED FileSystem* fs, void* h) { return h != NULL ? ((N3DSMemFile*) h)->pos : 0; }

static bool cBinarySeek(MAYBE_UNUSED FileSystem* fs, void* h, int32_t pos) {
    if (h == NULL || 0 > pos) return false;
    ((N3DSMemFile*) h)->pos = pos;
    return true;
}

static int32_t cBinarySize(MAYBE_UNUSED FileSystem* fs, void* h) { return h != NULL ? ((N3DSMemFile*) h)->size : 0; }

static void cBinaryRewrite(MAYBE_UNUSED FileSystem* fs, void* h) {
    N3DSMemFile* f = h;
    if (f == NULL) return;
    f->size = 0;
    f->pos = 0;
    f->dirty = true;
}

// ===[ Directories ]===
// Rare; everything pending goes to the card first so the listing and the files agree.

static bool cDirectoryExists(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    ptrdiff_t i = shgeti(c->dirs, p);
    if (i >= 0) return c->dirs[i].value;
    bool exists = TIMED(c->inner->vtable->directoryExists(c->inner, p));
    shput(c->dirs, p, exists);
    return exists;
}

static bool cCreateDirectory(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem_flush(fs, true);
    invalidate(fs);
    return TIMED(INNER(fs)->vtable->createDirectory(INNER(fs), p));
}

static bool cDeleteDirectory(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem_flush(fs, true);
    invalidate(fs);
    return TIMED(INNER(fs)->vtable->deleteDirectory(INNER(fs), p));
}

static FileSystemDirEntry* cListDirectory(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem_flush(fs, true);
    return TIMED(INNER(fs)->vtable->listDirectory(INNER(fs), p));
}

static FileSystemVtable gCachedVtable = {
    .resolvePath = cResolvePath,
    .fileExists = cFileExists,
    .readFileText = cReadFileText,
    .writeFileText = cWriteFileText,
    .deleteFile = cDeleteFile,
    .readFileBinary = cReadFileBinary,
    .writeFileBinary = cWriteFileBinary,
    .renameFile = cRenameFile,
    .binaryOpen = cBinaryOpen,
    .binaryClose = cBinaryClose,
    .binaryRead = cBinaryRead,
    .binaryWrite = cBinaryWrite,
    .binaryTell = cBinaryTell,
    .binarySeek = cBinarySeek,
    .binarySize = cBinarySize,
    .binaryRewrite = cBinaryRewrite,
    .directoryExists = cDirectoryExists,
    .createDirectory = cCreateDirectory,
    .deleteDirectory = cDeleteDirectory,
    .listDirectory = cListDirectory,
};

FileSystem* N3DSCachedFileSystem_create(FileSystem* inner) {
    N3DSCachedFileSystem* c = safeCalloc(1, sizeof(N3DSCachedFileSystem));
    c->base.vtable = &gCachedVtable;
    c->inner = inner;
    sh_new_strdup(c->files);
    sh_new_strdup(c->dirs);
    sh_new_strdup(c->contents);
    return (FileSystem*) c;
}

FileSystem* N3DSCachedFileSystem_destroy(FileSystem* fs) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    N3DSCachedFileSystem_flush(fs, true);
    FileSystem* inner = c->inner;
    for (ptrdiff_t i = 0; i < shlen(c->contents); i++) freeEntry(&c->contents[i].value);
    shfree(c->contents);
    shfree(c->files);
    shfree(c->dirs);
    free(c);
    return inner;
}
