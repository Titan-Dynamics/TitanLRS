#pragma once
#include <stddef.h>

// Builds a GET /config JSON response matching the ESP format.
// Writes into buf (null-terminated). Returns bytes written (excluding null).
// Output is truncated if buf is too small.
int buildConfigJSON(char* buf, size_t maxLen);
