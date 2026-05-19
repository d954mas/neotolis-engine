#include "ui/nt_ui.h"

/*
 * Clay v0.14 implementation TU.
 *
 * One TU in the entire build defines CLAY_IMPLEMENTATION. This is it.
 *
 * Version pin (per Drift 1 Option D):
 *   - deps/clay/VERSION is the single source of truth.
 *   - engine/ui/CMakeLists.txt parses major/minor and passes them as
 *     CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR via target_compile_definitions.
 *   - The _Static_assert below catches accidental dev-time drift (e.g.,
 *     someone hand-edits deps/clay/VERSION to 0.13 without re-vendoring).
 *   - CLAY_VERSION_MAJOR / CLAY_VERSION_MINOR macros do NOT exist in Clay
 *     v0.14 upstream -- verified by direct read of the v0.14 header.
 */

#if !defined(CLAY_PINNED_MAJOR) || !defined(CLAY_PINNED_MINOR)
#error "nt_ui: CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR must be defined by CMake (engine/ui/CMakeLists.txt parses deps/clay/VERSION)"
#endif

#define CLAY_IMPLEMENTATION
#include "clay.h"

_Static_assert(CLAY_PINNED_MAJOR == 0 && CLAY_PINNED_MINOR == 14, "Clay v0.14 required -- deps/clay/VERSION disagrees with the engine pin");
