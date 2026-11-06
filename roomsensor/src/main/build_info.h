#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This build's identity, stamped by build_info.sh on every build. deploy_ota.sh puts the same
// values in the OTA manifest (from build/build_info.json), so OTA compares like with like.

// Short git hash, or revYYYYMMDDHHMMSS-<hash>-dirty for a build from a modified tree
extern const char BUILD_INFO_VERSION[];
// When it was built, in seconds since the epoch (UTC)
extern const int64_t BUILD_INFO_TIMESTAMP;

#ifdef __cplusplus
}
#endif
