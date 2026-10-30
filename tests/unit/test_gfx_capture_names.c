#include <string.h>

#include "graphics/nt_gfx_capture_text.h"
#include "unity.h"

/* Web names are a prefix of native ones: both builds read the shared names; only native knows buffer readback. */

void setUp(void) {}
void tearDown(void) {}

#if NT_GFX_CAPTURE_ENABLED
#define NT_HEADER_ "ntcapture 2\nframe sequence=3 platform=native records=1 overflow=0 execute=0..0\n"

static nt_gfx_capture_parsed_t parse(const char *text) {
    static nt_gfx_event_t events[2];
    return nt_gfx_capture_parse(text, strlen(text), events, 2);
}

static void test_shared_names_read_on_every_platform(void) {
    const nt_gfx_capture_parsed_t r = parse(NT_HEADER_ "counter gl.glClear=1\n0 BACKEND op=CLEAR call=glClear args=[16384]\n");
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_EQUAL_UINT32(1, r.view.count);
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_NATIVE, r.view.platform);
}

static void test_native_only_names_read_only_on_native(void) {
#if defined(NT_PLATFORM_WEB)
    const uint32_t record = 0;
    const bool header = false;
#else
    const uint32_t record = 1;
    const bool header = true;
#endif
    TEST_ASSERT_EQUAL_UINT32(record, parse(NT_HEADER_ "0 OP op=BUFFER_READ\n").view.count);
    TEST_ASSERT_EQUAL_UINT32(record, parse(NT_HEADER_ "0 BACKEND op=STATE call=glMapBufferRange\n").view.count);
    TEST_ASSERT_EQUAL(header, parse(NT_HEADER_ "counter accepted.BUFFER_WAIT=1\n0 OP op=PASS\n").view.events != NULL);
    TEST_ASSERT_EQUAL(header, parse(NT_HEADER_ "counter gl.glFenceSync=1\n0 OP op=PASS\n").view.events != NULL);
}
#endif

int main(void) {
    UNITY_BEGIN();
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_shared_names_read_on_every_platform);
    RUN_TEST(test_native_only_names_read_only_on_native);
#endif
    return UNITY_END();
}
