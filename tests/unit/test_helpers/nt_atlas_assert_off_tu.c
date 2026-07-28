/* Recompile nt_atlas.c with asserts forced OFF regardless of the build's global
 * NT_ASSERT_MODE (CI release passes -DNT_ASSERT_MODE=2; a per-target -D would
 * collide). #undef beats the command line, so this pin works in every config. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "atlas/nt_atlas.c"
