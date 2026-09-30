/* Recompile the frontend with assertions disabled to exercise hard guards. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "graphics/nt_gfx.c"
