#pragma once

#include "../file_system.h"

// Wraps a FileSystem to keep SD access out of the frame loop: existence checks are cached, and the game's files
// are held in memory and written back later (see n3ds_cached_file_system.c).
FileSystem* N3DSCachedFileSystem_create(FileSystem* inner);
// Writes back the files the game has changed and then left alone for a while (all of them with force). Call once a
// frame, outside the game's step.
void N3DSCachedFileSystem_flush(FileSystem* fs, bool force);
// True while the game has changes not yet on the card (waiting to settle, or being written).
bool N3DSCachedFileSystem_isSaving(FileSystem* fs);
// Writes back everything, frees the wrapper and returns the wrapped file system.
FileSystem* N3DSCachedFileSystem_destroy(FileSystem* fs);
