#include "n3ds_cached_file_system.h"

#include "../utils.h"

#include <stb/ds/stb_ds.h>
#include <stdlib.h>

// Every file_exists/directory_exists is a real SD-card stat on the 3DS (milliseconds each); AM2R alone checks
// for a temp screenshot file every frame. Existence answers are cached and the whole cache is dropped on any
// change made through this file system, which is the only thing writing to the game folder while it runs.

typedef struct {
    char* key;
    bool value;
} N3DSExistsEntry;

typedef struct {
    FileSystem base;
    FileSystem* inner;
    N3DSExistsEntry* files;
    N3DSExistsEntry* dirs;
} N3DSCachedFileSystem;

#define INNER(fs) (((N3DSCachedFileSystem*) (fs))->inner)

static void invalidate(FileSystem* fs) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    shfree(c->files);
    shfree(c->dirs);
    sh_new_strdup(c->files);
    sh_new_strdup(c->dirs);
}

static char* cResolvePath(FileSystem* fs, const char* p) { return INNER(fs)->vtable->resolvePath(INNER(fs), p); }

static bool cFileExists(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    ptrdiff_t i = shgeti(c->files, p);
    if (i >= 0) return c->files[i].value;
    bool exists = c->inner->vtable->fileExists(c->inner, p);
    shput(c->files, p, exists);
    return exists;
}

static char* cReadFileText(FileSystem* fs, const char* p) { return INNER(fs)->vtable->readFileText(INNER(fs), p); }
static bool cReadFileBinary(FileSystem* fs, const char* p, uint8_t** d, int32_t* n) { return INNER(fs)->vtable->readFileBinary(INNER(fs), p, d, n); }

static bool cWriteFileText(FileSystem* fs, const char* p, const char* s) {
    invalidate(fs);
    return INNER(fs)->vtable->writeFileText(INNER(fs), p, s);
}

static bool cDeleteFile(FileSystem* fs, const char* p) {
    invalidate(fs);
    return INNER(fs)->vtable->deleteFile(INNER(fs), p);
}

static bool cWriteFileBinary(FileSystem* fs, const char* p, const uint8_t* d, int32_t n) {
    invalidate(fs);
    return INNER(fs)->vtable->writeFileBinary(INNER(fs), p, d, n);
}

static bool cRenameFile(FileSystem* fs, const char* a, const char* b) {
    invalidate(fs);
    return INNER(fs)->vtable->renameFile(INNER(fs), a, b);
}

static void* cBinaryOpen(FileSystem* fs, const char* p, int32_t mode) {
    if (mode != GML_FILE_BIN_READ) invalidate(fs);
    return INNER(fs)->vtable->binaryOpen(INNER(fs), p, mode);
}

static void cBinaryClose(FileSystem* fs, void* h) { INNER(fs)->vtable->binaryClose(INNER(fs), h); }
static int32_t cBinaryRead(FileSystem* fs, void* h, void* d, int32_t n) { return INNER(fs)->vtable->binaryRead(INNER(fs), h, d, n); }
static int32_t cBinaryWrite(FileSystem* fs, void* h, const void* s, int32_t n) { return INNER(fs)->vtable->binaryWrite(INNER(fs), h, s, n); }
static int32_t cBinaryTell(FileSystem* fs, void* h) { return INNER(fs)->vtable->binaryTell(INNER(fs), h); }
static bool cBinarySeek(FileSystem* fs, void* h, int32_t pos) { return INNER(fs)->vtable->binarySeek(INNER(fs), h, pos); }
static int32_t cBinarySize(FileSystem* fs, void* h) { return INNER(fs)->vtable->binarySize(INNER(fs), h); }
static void cBinaryRewrite(FileSystem* fs, void* h) { INNER(fs)->vtable->binaryRewrite(INNER(fs), h); }

static bool cDirectoryExists(FileSystem* fs, const char* p) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    ptrdiff_t i = shgeti(c->dirs, p);
    if (i >= 0) return c->dirs[i].value;
    bool exists = c->inner->vtable->directoryExists(c->inner, p);
    shput(c->dirs, p, exists);
    return exists;
}

static bool cCreateDirectory(FileSystem* fs, const char* p) {
    invalidate(fs);
    return INNER(fs)->vtable->createDirectory(INNER(fs), p);
}

static bool cDeleteDirectory(FileSystem* fs, const char* p) {
    invalidate(fs);
    return INNER(fs)->vtable->deleteDirectory(INNER(fs), p);
}

static FileSystemDirEntry* cListDirectory(FileSystem* fs, const char* p) { return INNER(fs)->vtable->listDirectory(INNER(fs), p); }

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
    return (FileSystem*) c;
}

FileSystem* N3DSCachedFileSystem_destroy(FileSystem* fs) {
    N3DSCachedFileSystem* c = (N3DSCachedFileSystem*) fs;
    FileSystem* inner = c->inner;
    shfree(c->files);
    shfree(c->dirs);
    free(c);
    return inner;
}
