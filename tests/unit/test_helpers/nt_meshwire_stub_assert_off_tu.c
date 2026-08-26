/* Recompile the meshwire stub with asserts forced OFF for the failure-return
 * contract test. #undef avoids colliding with CI's global NT_ASSERT_MODE. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "meshwire/stub/nt_meshwire_stub.c"
