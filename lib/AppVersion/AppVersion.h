#pragma once

// PlatformIO normally supplies these through build_flags/extra_scripts. Keep
// fallbacks here so editor indexers and simulator-like tools still parse files.
#ifndef CROSSINK_VERSION
#define CROSSINK_VERSION "dev"
#endif

#ifndef CROSSINK_GIT_SHA
#define CROSSINK_GIT_SHA "unknown"
#endif

#ifndef CROSSINK_GIT_DIRTY
#define CROSSINK_GIT_DIRTY "unknown"
#endif

#ifndef CROSSINK_BUILD_ENV
#define CROSSINK_BUILD_ENV "unknown"
#endif

#ifndef CROSSINK_FIRMWARE_DEVICE_TYPE
#define CROSSINK_FIRMWARE_DEVICE_TYPE "unknown"
#endif

// CrossBlot's release version for the boot screen. CROSSINK_VERSION (macro name
// kept for the build scripts) carries the same number from platformio.ini.
#ifndef CROSSBLOT_VERSION
#define CROSSBLOT_VERSION "1.0.0"
#endif
