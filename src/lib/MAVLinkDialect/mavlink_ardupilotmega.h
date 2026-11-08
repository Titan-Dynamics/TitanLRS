#pragma once

// Include the MAVLink ardupilotmega dialect (a superset of common) through here, not
// "ardupilotmega/mavlink.h" directly. See mavlink_common.h for why the warning is silenced.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#include "ardupilotmega/mavlink.h"
#pragma GCC diagnostic pop
