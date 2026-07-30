/* Recompile nt_atlas.c with asserts forced OFF for the parser-contract test.
 * #undef avoids colliding with CI's global NT_ASSERT_MODE definition. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "atlas/nt_atlas.c"
