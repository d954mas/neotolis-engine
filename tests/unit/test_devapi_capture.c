/* L2 capture group via submit() (no socket): bot-param validation -> bad_params (never assert), the
   defer path (submit -> NULL), and encode at the pre-swap seam (stub window swap) against nt_gfx_fake. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "app/nt_app.h"
// NOLINTNEXTLINE(bugprone-suspicious-include): value-test the static strip_and_box.
#include "devapi/nt_devapi_capture.c"
#include "devapi/nt_devapi_internal.h"
#include "graphics/nt_gfx.h" /* g_nt_gfx.context_lost — the producer-failure (NULL) trigger. */
#include "window/nt_window.h"
#include "unity.h"
/* clang-format on */

#define CAP_FB_W 64
#define CAP_FB_H 48

void setUp(void) {
    /* No nt_fpng_init() here: registering the capture group (register_default -> register_capture) inits
       the encoder. This test proves a host needs no explicit fpng init. */
    /* frame never advances here: capture must resolve with the sim frozen. */
    g_nt_app = (nt_app_t){.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};
    g_nt_window.fb_width = CAP_FB_W;
    g_nt_window.fb_height = CAP_FB_H;
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_capture(); /* also installs the pre-swap hook that nt_window_swap_buffers runs. */
    /* The seam runs between end_frame and the swap, where readback is allowed. */
    const nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_devapi_shutdown();
}

/* ---- helpers ---- */

/* One host tick of the devapi: nt_devapi_update resolves the pending gate, then the drain yields it. */
static const char *drain(void) {
    nt_devapi_update();
    return nt_devapi_poll_response();
}

static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp); /* a sync error returns an envelope, never NULL (NULL == deferred). */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

/* Drive one render seam (no frame advance), then poll the yielded envelope (caller frees the root). */
static cJSON *capture_yield(const char *request) {
    TEST_ASSERT_NULL(nt_devapi_submit(request)); /* a well-formed capture defers (no sync response). */
    nt_window_swap_buffers();                    /* the pre-swap seam builds the reply. */
    const char *resp = drain();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

static cJSON *result_of(cJSON *root) { return cJSON_GetObjectItemCaseSensitive(root, "result"); }

/* bad scale -> bad_params */

static void test_capture_frame_bad_scale_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.frame\",\"params\":{\"scale\":3}}"));
    /* non-number scale also rejected, never asserts. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.frame\",\"params\":{\"scale\":\"half\"}}"));
}

/* bad region -> bad_params */

static void test_capture_region_out_of_bounds_bad_params(void) {
    /* x + w > fb_width. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":40,\"y\":0,\"w\":40,\"h\":10}}"));
    /* zero width. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":0,\"w\":0,\"h\":10}}"));
    /* negative width parses as out-of-range u32 -> bad_params (never assert). */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":0,\"w\":-1,\"h\":10}}"));
    /* y + h > fb_height. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":40,\"w\":10,\"h\":40}}"));
}

/* a well-formed capture DEFERS (returns NULL, no sync yield, no assert) */

static void test_capture_frame_defers(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"capture.frame\",\"request_id\":1}");
    TEST_ASSERT_NULL(resp); /* deferred sentinel: no synchronous response. */
    /* before the seam runs, polling yields nothing (the slot has no payload yet). */
    TEST_ASSERT_NULL(drain());
}

/* seam-driven encode (no socket) -> format:"png" + non-empty data */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_capture_frame_seam_encode(void) {
    cJSON *root = capture_yield("{\"method\":\"capture.frame\",\"request_id\":7}");
    cJSON *result = result_of(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(result));

    cJSON *format = cJSON_GetObjectItemCaseSensitive(result, "format");
    TEST_ASSERT_TRUE(cJSON_IsString(format));
    TEST_ASSERT_EQUAL_STRING("png", format->valuestring);

    cJSON *data = cJSON_GetObjectItemCaseSensitive(result, "data");
    TEST_ASSERT_TRUE(cJSON_IsString(data));
    TEST_ASSERT_TRUE(strlen(data->valuestring) > 0); /* non-empty base64 PNG. */

    /* full-frame dims = the framebuffer (scale 1). */
    TEST_ASSERT_EQUAL_INT(CAP_FB_W, cJSON_GetObjectItemCaseSensitive(result, "width")->valueint);
    TEST_ASSERT_EQUAL_INT(CAP_FB_H, cJSON_GetObjectItemCaseSensitive(result, "height")->valueint);

    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(7, id->valueint); /* request_id correlation preserved. */
    cJSON_Delete(root);
    TEST_ASSERT_NULL(drain()); /* drained. */
}

/* scale + region produce POST-scale dims */

static void test_capture_frame_scale_halves_dims(void) {
    cJSON *root = capture_yield("{\"method\":\"capture.frame\",\"params\":{\"scale\":2}}");
    cJSON *result = result_of(root);
    /* ½ downscale: post-scale dims = fb / 2. */
    TEST_ASSERT_EQUAL_INT(CAP_FB_W / 2, cJSON_GetObjectItemCaseSensitive(result, "width")->valueint);
    TEST_ASSERT_EQUAL_INT(CAP_FB_H / 2, cJSON_GetObjectItemCaseSensitive(result, "height")->valueint);
    TEST_ASSERT_EQUAL_STRING("png", cJSON_GetObjectItemCaseSensitive(result, "format")->valuestring);
    cJSON_Delete(root);
}

static void test_capture_region_dims_match_rect(void) {
    cJSON *root = capture_yield("{\"method\":\"capture.region\",\"params\":{\"x\":8,\"y\":4,\"w\":32,\"h\":16}}");
    cJSON *result = result_of(root);
    /* sub-rect, no resampling: dims = the requested w/h. */
    TEST_ASSERT_EQUAL_INT(32, cJSON_GetObjectItemCaseSensitive(result, "width")->valueint);
    TEST_ASSERT_EQUAL_INT(16, cJSON_GetObjectItemCaseSensitive(result, "height")->valueint);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(result, "data");
    TEST_ASSERT_TRUE(cJSON_IsString(data) && strlen(data->valuestring) > 0);
    cJSON_Delete(root);
}

/* region + scale -> POST-scale dims (region scale=2 = rect / 2) */

static void test_capture_region_scale_halves_dims(void) {
    cJSON *root = capture_yield("{\"method\":\"capture.region\",\"params\":{\"x\":8,\"y\":4,\"w\":32,\"h\":16,\"scale\":2}}");
    cJSON *result = result_of(root);
    /* scale applies AFTER the crop: post-scale dims = rect w/h / 2. */
    TEST_ASSERT_EQUAL_INT(16, cJSON_GetObjectItemCaseSensitive(result, "width")->valueint);
    TEST_ASSERT_EQUAL_INT(8, cJSON_GetObjectItemCaseSensitive(result, "height")->valueint);
    TEST_ASSERT_EQUAL_STRING("png", cJSON_GetObjectItemCaseSensitive(result, "format")->valuestring);
    cJSON_Delete(root);
}

/* degenerate scale>rect -> SYNCHRONOUS bad_params */

static void test_capture_region_degenerate_scale_bad_params(void) {
    /* w/scale==0 (3/4==0): the handler rejects synchronously, no defer, no image. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":0,\"w\":3,\"h\":3,\"scale\":4}}"));
}

/* scale that does not EVENLY divide the dims -> bad_params, not a silent edge crop: floor(6/4)==1 is
   non-zero, so a floor-div would accept it and drop the 2 remainder pixels per axis. */
static void test_capture_nondividing_scale_bad_params(void) {
    /* region 6x6 scale 4: 6%4==2 (non-zero floor, but not an even divide) -> bad_params. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":0,\"w\":6,\"h\":6,\"scale\":4}}"));
    /* frame: a framebuffer not divisible by the scale is rejected too (whole-frame edge crop). */
    g_nt_window.fb_width = 66U; /* 66 % 4 == 2; setUp restores CAP_FB_W before the next test. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.frame\",\"params\":{\"scale\":4}}"));
}

/* the reply ran but the readback failed -> {ok:false,error:capture_failed} with the correlated id. A lost
   context makes nt_gfx_read_pixels fail. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_capture_producer_failure_yields_error(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"capture.frame\",\"request_id\":42}"));
    g_nt_gfx.context_lost = true; /* force the readback to fail. */
    nt_window_swap_buffers();
    g_nt_gfx.context_lost = false; /* restore for any following test. */

    const char *resp = drain();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"))); /* ok:false, not a success shape. */
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON *code = cJSON_GetObjectItemCaseSensitive(error, "code");
    TEST_ASSERT_TRUE(cJSON_IsString(code));
    TEST_ASSERT_EQUAL_STRING("capture_failed", code->valuestring);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "data")); /* no image. */
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(42, id->valueint); /* request_id correlation preserved on the failure path. */
    cJSON_Delete(root);
    TEST_ASSERT_NULL(drain());
}

/* pixel cap (DoS backstop) rejects an oversized framebuffer/rect synchronously.
   A framebuffer/rect above NT_DEVAPI_CAPTURE_MAX_PIXELS (default 4096^2) -> bad_params, no defer, no
   allocation — the security-relevant cap branch that the 64x48 fixture otherwise can't reach. */
static void test_capture_pixel_cap_bad_params(void) {
    g_nt_window.fb_width = 5000U; /* 5000*5000 = 25M px > the 16.7M (4096^2) cap. */
    g_nt_window.fb_height = 5000U;
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.frame\"}"));
    /* capture.region over a too-large but in-bounds rect trips the same cap (not the bounds check). */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":0,\"w\":5000,\"h\":5000}}"));
    /* setUp() restores CAP_FB_W/H before the next test. */
}

/* a poll before the seam waits (the image does not exist yet), even with the sim frozen in idle MANUAL;
   the seam then builds the real PNG. */
static void test_capture_waits_for_the_seam_with_sim_frozen(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL; /* no step queued */
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"capture.frame\",\"request_id\":9}"));
    TEST_ASSERT_NULL(drain());
    nt_window_swap_buffers();
    const char *resp = drain();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_STRING("png", cJSON_GetObjectItemCaseSensitive(result, "format")->valuestring);
    TEST_ASSERT_EQUAL_INT(9, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
    TEST_ASSERT_NULL(drain());
}

/* rendering off -> no frame is presented, so the capture fails as stalled on the drain instead of hanging. */
static void test_capture_with_render_off_stalls(void) {
    nt_app_set_render_enabled(false);
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"capture.frame\",\"request_id\":3}"));
    const char *resp = drain();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL_STRING("stalled", cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "error"), "code")->valuestring);
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetObjectItemCaseSensitive(root, "request_id")->valueint);
    cJSON_Delete(root);
}

/* box-average downscale produces the EXACT integer mean.
   Hermetic value check on the averager: the dims-only scale tests can't catch a wrong divisor (/factor vs
   /area) or wrong in-box source pixels. A 2x2 -> 1x1 box has a hand-computable per-channel mean. */
static void test_capture_box_average_exact_mean(void) {
    /* 2x2 rgba8 source (row-major); the alpha is dropped by the strip. */
    const uint8_t src[2 * 2 * 4] = {
        10, 20, 30, 255, 20, 40, 60,  255, /* row 0 */
        30, 60, 90, 255, 40, 80, 120, 255, /* row 1 */
    };
    uint8_t dst[3] = {0, 0, 0};
    strip_and_box(src, 2, 2, 2, dst);
    /* mean over the 4-texel box: R=(10+20+30+40)/4=25, G=(20+40+60+80)/4=50, B=(30+60+90+120)/4=75. */
    TEST_ASSERT_EQUAL_UINT8(25, dst[0]);
    TEST_ASSERT_EQUAL_UINT8(50, dst[1]);
    TEST_ASSERT_EQUAL_UINT8(75, dst[2]);

    /* factor==1 is a plain alpha-strip (RGB passthrough, alpha dropped). */
    const uint8_t one[4] = {11, 22, 33, 99};
    uint8_t out1[3] = {0, 0, 0};
    strip_and_box(one, 1, 1, 1, out1);
    TEST_ASSERT_EQUAL_UINT8(11, out1[0]);
    TEST_ASSERT_EQUAL_UINT8(22, out1[1]);
    TEST_ASSERT_EQUAL_UINT8(33, out1[2]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_capture_frame_bad_scale_bad_params);
    RUN_TEST(test_capture_region_out_of_bounds_bad_params);
    RUN_TEST(test_capture_frame_defers);
    RUN_TEST(test_capture_frame_seam_encode);
    RUN_TEST(test_capture_frame_scale_halves_dims);
    RUN_TEST(test_capture_region_dims_match_rect);
    RUN_TEST(test_capture_region_scale_halves_dims);
    RUN_TEST(test_capture_region_degenerate_scale_bad_params);
    RUN_TEST(test_capture_nondividing_scale_bad_params);
    RUN_TEST(test_capture_producer_failure_yields_error);
    RUN_TEST(test_capture_pixel_cap_bad_params);
    RUN_TEST(test_capture_waits_for_the_seam_with_sim_frozen);
    RUN_TEST(test_capture_with_render_off_stalls);
    RUN_TEST(test_capture_box_average_exact_mean);
    return UNITY_END();
}
