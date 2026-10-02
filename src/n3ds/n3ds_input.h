#pragma once

#include <3ds/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef RUNNER_DEFINED
#define RUNNER_DEFINED
typedef struct Runner Runner;
#endif
typedef struct Renderer Renderer;

void N3DSInput_init(Runner* runner);
// Reads the buttons (plus harness.txt presses) into the keyboard and gamepad slot 0. Returns newly pressed keys.
u32 N3DSInput_update(Runner* runner);
bool N3DSInput_exitRequested(void);

// Self-screenshots of the top screen (PNG under N3DS_SD_DIR "shots/").
void N3DSScreenshot_request(int32_t frame);
// Call right after C3D_FrameBegin: the previous frame is finished and still in the top screen's color buffer.
void N3DSScreenshot_captureIfRequested(Renderer* renderer);
