/* L2 devapi capture group (capture.frame / capture.region) via submit() (no socket): the FIRST
   deferred command that returns DATA. Asserts the bot-param validation (bad scale / out-of-bounds
   region / zero-size -> bad_params, NEVER assert), the defer path (submit returns NULL, no sync
   yield), and the seam-driven encode end-to-end against the nt_gfx_stub synthetic buffer (no GL):
   driving nt_devapi_capture_on_pre_swap then polling yields {format:"png", non-empty data} with
   the post-scale dims. The whole TU is gated on NT_DEVAPI_GROUP_CAPTURE so the OFF mirror links a
   stub main (zero release delta). */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(NT_DEVAPI_GROUP_CAPTURE)

/* clang-format off */
#include "app/nt_app.h"
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
    g_nt_app.frame = 0; /* deferred targets are g_nt_app.frame + 1 — start at a known frame. */
    g_nt_window.fb_width = CAP_FB_W;
    g_nt_window.fb_height = CAP_FB_H;
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_default();
    nt_devapi_capture_arm(); /* mark capture-capable: the test drives the seam directly, no window hook. */
}

void tearDown(void) { nt_devapi_shutdown(); }

/* ---- helpers ---- */

static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp); /* a sync error returns an envelope, never NULL (NULL == deferred). */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

/* Drive one render seam + frame advance, then poll the yielded envelope (caller frees the root). */
static cJSON *capture_yield(const char *request) {
    TEST_ASSERT_NULL(nt_devapi_submit(request)); /* a well-formed capture defers (no sync response). */
    g_nt_app.frame++;                            /* reach the slot's 1-frame target. */
    nt_devapi_capture_on_pre_swap();             /* GL-valid seam: fills the payload via the producer. */
    const char *resp = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

static cJSON *result_of(cJSON *root) { return cJSON_GetObjectItemCaseSensitive(root, "result"); }

/* ---- Test 1: bad scale -> bad_params ---- */

static void test_capture_frame_bad_scale_bad_params(void) {
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.frame\",\"params\":{\"scale\":3}}"));
    /* non-number scale also rejected, never asserts. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.frame\",\"params\":{\"scale\":\"half\"}}"));
}

/* ---- Test 2: bad region -> bad_params ---- */

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

/* ---- Test 3: a well-formed capture DEFERS (returns NULL, no sync yield, no assert) ---- */

static void test_capture_frame_defers(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"capture.frame\",\"request_id\":1}");
    TEST_ASSERT_NULL(resp); /* deferred sentinel: no synchronous response. */
    /* before the seam runs, polling yields nothing (the slot has no payload yet). */
    TEST_ASSERT_NULL(nt_devapi_poll_response());
}

/* ---- Test 4: seam-driven encode (no socket) -> format:"png" + non-empty data ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_capture_frame_seam_encode(void) {
    cJSON *root = capture_yield("{\"method\":\"capture.frame\",\"request_id\":7}");
    cJSON *result = result_of(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    /* the stored payload was yielded, NOT the legacy {deferred:true}. */
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(result, "deferred"));

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
    TEST_ASSERT_NULL(nt_devapi_poll_response()); /* queue drained. */
}

/* ---- Test 5: scale + region produce POST-scale dims ---- */

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

/* ---- Test 6: region + scale -> POST-scale dims (region scale=2 = rect / 2) ---- */

static void test_capture_region_scale_halves_dims(void) {
    cJSON *root = capture_yield("{\"method\":\"capture.region\",\"params\":{\"x\":8,\"y\":4,\"w\":32,\"h\":16,\"scale\":2}}");
    cJSON *result = result_of(root);
    /* scale applies AFTER the crop: post-scale dims = rect w/h / 2. */
    TEST_ASSERT_EQUAL_INT(16, cJSON_GetObjectItemCaseSensitive(result, "width")->valueint);
    TEST_ASSERT_EQUAL_INT(8, cJSON_GetObjectItemCaseSensitive(result, "height")->valueint);
    TEST_ASSERT_EQUAL_STRING("png", cJSON_GetObjectItemCaseSensitive(result, "format")->valuestring);
    cJSON_Delete(root);
}

/* ---- Test 7: degenerate scale>rect -> SYNCHRONOUS bad_params (never {deferred:true}) ---- */

static void test_capture_region_degenerate_scale_bad_params(void) {
    /* w/scale==0 (3/4==0): the handler rejects synchronously, no defer, no image. */
    assert_bad_params(nt_devapi_submit("{\"method\":\"capture.region\",\"params\":{\"x\":0,\"y\":0,\"w\":3,\"h\":3,\"scale\":4}}"));
}

/* ---- Test 8: producer RAN but returned NULL -> {ok:false,error:capture_failed} with correlated id ----
   Drives a lost context so nt_gfx_read_pixels returns false -> the producer yields NULL -> the DATA slot
   resolves as a distinguishable capture_failed error (NOT the content-free legacy {deferred:true}, NOT a
   crash). A client thus never sees ok:true without the documented {width,height,format,data} shape. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_capture_producer_failure_yields_error(void) {
    TEST_ASSERT_NULL(nt_devapi_submit("{\"method\":\"capture.frame\",\"request_id\":42}"));
    g_nt_gfx.context_lost = true; /* force the readback to fail -> producer returns NULL. */
    g_nt_app.frame++;
    nt_devapi_capture_on_pre_swap();
    g_nt_gfx.context_lost = false; /* restore for any following test. */

    const char *resp = nt_devapi_poll_response();
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok"))); /* ok:false, not a success shape. */
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_NOT_NULL(error);
    cJSON *code = cJSON_GetObjectItemCaseSensitive(error, "code");
    TEST_ASSERT_TRUE(cJSON_IsString(code));
    TEST_ASSERT_EQUAL_STRING("capture_failed", code->valuestring);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(root, "data")); /* no image, no legacy deferred flag. */
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(42, id->valueint); /* request_id correlation preserved on the failure path. */
    cJSON_Delete(root);
    TEST_ASSERT_NULL(nt_devapi_poll_response());
}

/* ---- Test 9: in-flight capture cap -> a flood is rejected with bad_params, never queued unbounded ----
   Captures defer (return NULL); once NT_DEVAPI_CAPTURE_MAX_INFLIGHT are pending, the next is rejected
   synchronously, well before the 128-slot deferred queue would fill (bounds the per-seam encode burst). */
static void test_capture_inflight_cap_rejects_flood(void) {
    int deferred = 0;
    const char *resp = NULL;
    for (int i = 0; i < 200; i++) {
        resp = nt_devapi_submit("{\"method\":\"capture.frame\"}");
        if (resp != NULL) {
            break; /* a synchronous response == rejection (an accepted capture defers -> NULL). */
        }
        deferred++;
    }
    TEST_ASSERT_TRUE(deferred >= 1);  /* captures are accepted... */
    TEST_ASSERT_NOT_NULL(resp);       /* ...then the flood is rejected... */
    TEST_ASSERT_TRUE(deferred <= 64); /* ...well before the 128-slot deferred queue fills (the in-flight cap tripped). */
    assert_bad_params(resp);          /* clean bad_params, never an assert/crash. */
    nt_devapi_deferred_reset();       /* clear pending slots so following tests start clean. */
}

/* ---- Test 10: pixel cap (DoS backstop) rejects an oversized framebuffer/rect synchronously ----
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

/* ---- Test 11: a host that never drives the seam -> capture rejected synchronously, never hangs ----
   Re-init WITHOUT arming (a host like devapi_host that registers capture but inits no GL / no seam):
   capture.frame must return {ok:false,error:capture_unavailable}, not defer a slot that never resolves. */
static void test_capture_unarmed_host_unavailable(void) {
    nt_devapi_shutdown();                       /* disarms the seam (resp_reset). */
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init()); /* unarmed: no seam call follows. */
    nt_devapi_register_default();               /* capture registered but NOT armed. */
    const char *resp = nt_devapi_submit("{\"method\":\"capture.frame\"}");
    TEST_ASSERT_NOT_NULL(resp); /* synchronous reject, not a deferred NULL. */
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *code = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "error"), "code");
    TEST_ASSERT_TRUE(cJSON_IsString(code));
    TEST_ASSERT_EQUAL_STRING("capture_unavailable", code->valuestring);
    cJSON_Delete(root);
    /* tearDown shuts down; the next setUp re-inits + re-arms. */
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
    RUN_TEST(test_capture_producer_failure_yields_error);
    RUN_TEST(test_capture_inflight_cap_rejects_flood);
    RUN_TEST(test_capture_pixel_cap_bad_params);
    RUN_TEST(test_capture_unarmed_host_unavailable);
    return UNITY_END();
}

#else /* !NT_DEVAPI_GROUP_CAPTURE — OFF-mirror: link a stub main so native-debug-off links clean. */

int main(void) {
    printf("test_devapi_capture: NT_DEVAPI_GROUP_CAPTURE off — nothing to test (zero release delta).\n");
    return 0;
}

#endif /* NT_DEVAPI_GROUP_CAPTURE */
