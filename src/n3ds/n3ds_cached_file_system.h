#pragma once

#include "../file_system.h"

// Wraps a FileSystem and caches file/directory existence checks (SD stat calls are slow on the 3DS).
FileSystem* N3DSCachedFileSystem_create(FileSystem* inner);
// Frees the wrapper and returns the wrapped file system.
FileSystem* N3DSCachedFileSystem_destroy(FileSystem* fs);
