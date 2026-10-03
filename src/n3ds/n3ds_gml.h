#pragma once

// Reading and writing a running game's variables from the 3DS front end (the pause screen's touch controls).
// Names are resolved once and cached; everything is a no-op returning false/NULL when a name doesn't exist.

#include "../runner.h"

#include <stdbool.h>
#include <stdint.h>

void N3DSGml_init(Runner* runner);

// Index of the object with this name, -1 if none.
int32_t N3DSGml_objectIndex(const char* name);
// Index of the room with this name, -1 if none.
int32_t N3DSGml_roomIndex(const char* name);

// First active, live instance of an object (by index), NULL if none.
Instance* N3DSGml_firstInstance(int32_t objectIndex);
// First live instance of an object, active or not, NULL if none.
Instance* N3DSGml_anyInstance(int32_t objectIndex);
// Fills out[] with up to max active, live instances of an object; returns how many.
int32_t N3DSGml_instances(int32_t objectIndex, Instance** out, int32_t max);

// global.<name> (or global.<name>[index] with index >= 0) as a number.
bool N3DSGml_getGlobal(const char* name, int32_t index, double* out);
void N3DSGml_setGlobal(const char* name, double value);
// <inst>.<name> as a number.
bool N3DSGml_getVar(Instance* inst, const char* name, double* out);
void N3DSGml_setVar(Instance* inst, const char* name, double value);

// Top-left of view 0 in room coordinates (where the screen's 0,0 is).
void N3DSGml_viewOrigin(float* x, float* y);
