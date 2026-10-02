#pragma once

#define N3DS_FORCE_OLD3DS_MODE 0

// Game folder on the SD card: data.win, preprocessed gfx/ and saves. romfs:/ is searched first.
#ifndef N3DS_SD_DIR
#define N3DS_SD_DIR "sdmc:/3ds/butterscotch/"
#endif
