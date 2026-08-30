/* Recompile nt_sprite_renderer.c with asserts forced OFF, so the pipeline-cache
 * capacity guard can be driven without the assert firing first. #undef avoids
 * colliding with CI's global NT_ASSERT_MODE definition. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0
#ifndef NT_TEST_ACCESS
#define NT_TEST_ACCESS
#endif
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "renderers/nt_sprite_renderer.c"
