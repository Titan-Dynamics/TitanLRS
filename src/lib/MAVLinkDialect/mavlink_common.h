#pragma once

// Include the MAVLink common dialect through here, not "common/mavlink.h" directly.
//
// c_library_v2's *_encode() helpers pass arrays inside packed structs to *_pack(), which only
// memcpy them; GCC warns -Waddress-of-packed-member on every includer. It's third-party noise, so
// it is silenced for the library headers only. -Wpragmas: GCC < 9 (ESP32) has no such option.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include "common/mavlink.h"
#pragma GCC diagnostic pop
