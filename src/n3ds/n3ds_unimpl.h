#pragma once

#include <stdint.h>

// Renderer entry points the 3DS backend does not implement yet. Each unique "function (args)" is logged once
// to the debug log (UNIMPL ...) and counted; the call itself draws nothing.
void N3DS_unimpl(const char* function, const char* argsFormat, ...) __attribute__((format(printf, 2, 3)));

// Writes every UNIMPL entry seen so far with its hit count (and how many were new since the last dump).
void N3DS_unimplDump(const char* reason);

// Number of unique UNIMPL entries seen so far.
uint32_t N3DS_unimplCount(void);

#define N3DS_UNIMPL(function, ...) N3DS_unimpl(function, __VA_ARGS__)
