#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_capture_text.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

static nt_program_t s_program;

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 256;
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 4096;
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    nt_gfx_init(&desc);
    s_program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_begin_frame();
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

#if NT_GFX_CAPTURE_ENABLED
static char *format_all(const nt_gfx_capture_view_t *view) {
    const size_t needed = nt_gfx_capture_format(view, NULL, 0);
    char *text = (char *)malloc(needed + 1);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_UINT64(needed, nt_gfx_capture_format(view, text, needed + 1));
    TEST_ASSERT_EQUAL_UINT64(needed, strlen(text));
    return text;
}

static void set_text(char *dst, const char *src) { memcpy(dst, src, strlen(src) + 1); }

static float float_from_bits(uint32_t bits) {
    float f = 0.0F;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

#define NT_RECORD_COUNT 23

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size) -- one record per data arm
static void build_records(nt_gfx_event_t *ev) {
    memset(ev, 0, NT_RECORD_COUNT * sizeof(*ev));
    ev[0] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_PASS, .result = NT_GFX_RESULT_ACCEPTED};
    ev[0].data.pass.target = 3;
    ev[0].data.pass.color[0] = 0.25F;
    ev[0].data.pass.color[3] = 1.0F;
    ev[0].data.pass.depth = 1.0F / 3.0F; /* the shortest exact form needs 8 digits */
    ev[0].data.pass.load_color = true;
    ev[1] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_BACKEND, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_GL_glClearColor, .link = 1, .arg_count = 4U << 4};
    ev[1].data.backend.values[0] = 0.25F;
    ev[1].data.backend.values[3] = 1.0F;
    /* A trailing zero argument is still an argument. */
    ev[2] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_BACKEND, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_GL_glGenBuffers, .arg_count = 2};
    ev[2].data.backend.args[0] = 1;
    ev[3] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_SKIP, .operation = NT_GFX_OP_TEXTURE, .result = NT_GFX_RESULT_CACHE, .detail = NT_GFX_GL_glBindTexture, .arg_count = 2};
    ev[3].data.backend.args[0] = 7;
    ev[4] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_SKIP, .operation = NT_GFX_OP_TEXTURE_SET, .result = NT_GFX_RESULT_INACTIVE};
    ev[4].data.binding.name = 99;
    ev[5].note.kind = NT_GFX_EVENT_MARKER_PUSH;
    set_text(ev[5].note.label, "frame \"a\"\n");
    ev[6].note.kind = NT_GFX_EVENT_NOTE;
    set_text(ev[6].note.label, "mesh");
    const nt_gfx_note_type_t types[5] = {NT_GFX_NOTE_U32, NT_GFX_NOTE_I32, NT_GFX_NOTE_U64, NT_GFX_NOTE_F32, NT_GFX_NOTE_STR};
    const char *keys[5] = {"material", "offset", "bits", "scale", "drop"};
    for (int i = 0; i < 5; i++) {
        ev[7 + i].field.kind = NT_GFX_EVENT_NOTE_FIELD;
        ev[7 + i].field.type = types[i];
        set_text(ev[7 + i].field.key, keys[i]);
    }
    ev[7].field.value.u32 = 5;
    ev[8].field.value.i32 = -2;
    ev[9].field.value.u64 = 1ULL << 60;
    ev[10].field.value.f32 = -0.0F;
    set_text(ev[11].field.value.str, "font not loaded");
    ev[12] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_DRAW_INDEXED, .object_kind = NT_GFX_OBJECT_PIPELINE, .object = 65537, .result = NT_GFX_RESULT_ACCEPTED};
    ev[12].data.draw.first = 6;
    ev[12].data.draw.count = 3;
    ev[12].data.draw.vertices = 4;
    ev[12].data.draw.instances = 1;
    ev[12].data.draw.vertex_input = 65538;
    ev[13] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 65539};
    ev[13].data.resource.backend = 2;
    ev[13].data.resource.related[0] = 5;
    ev[13].data.resource.width = 13;
    ev[13].data.resource.height = 7;
    ev[13].data.resource.format = 1;
    ev[13].data.resource.levels = 1;
    ev[14] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_OBJECT_TEXTURE};
    ev[14].data.backend.args[0] = 2;
    ev[14].data.backend.args[1] = 11;
    ev[15] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_ATTRIBUTE, .object_kind = NT_GFX_OBJECT_VERTEX_INPUT, .object = 65540};
    ev[15].data.attribute.buffer = 65541;
    ev[15].data.attribute.stride = 16;
    ev[15].data.attribute.location = 1;
    ev[15].data.attribute.type = 3;
    ev[15].data.attribute.count = 2;
    ev[16] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_INITIAL, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_INITIAL_BACKEND};
    ev[16].data.backend.args[0] = 4;
    ev[16].data.backend.args[4] = 640;
    ev[16].data.backend.args[5] = 480;
    ev[17] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_INITIAL, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_INITIAL_FRONTEND};
    ev[17].data.state.integers[0] = 1;
    ev[18] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_CLEAR, .result = NT_GFX_RESULT_ACCEPTED};
    ev[18].data.clear.clear_color[0] = float_from_bits(0x7fc00001U);
    ev[18].data.clear.clear_depth = 0.5F;
    ev[18].data.clear.color = true;
    ev[19].kind = NT_GFX_EVENT_MARKER_POP;
    ev[20] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_ARGUMENT, .operation = NT_GFX_OP_TEXTURE_SET, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 65539, .link = 1};
    ev[20].data.binding.secondary = 3;
    ev[20].data.binding.name = 42;
    ev[21] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_UBO, .object_kind = NT_GFX_OBJECT_BUFFER, .object = 65541};
    ev[21].data.binding.slot = 1;
    ev[21].data.binding.offset = 256;
    ev[21].data.binding.size = 64;
    ev[22] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_DRAW_INDEXED, .result = NT_GFX_RESULT_CACHE, .detail = 13};
    ev[22].data.draw.first = 9;
    ev[22].data.draw.count = 3;
}

static const char *const k_expected = "ntcapture 2\n"
                                      "frame sequence=9 platform=native records=23 overflow=1 execute=12..13\n"
                                      "counter vertices=4\n"
                                      "counter buffer_upload_bytes=1099511627776\n"
                                      "counter accepted.DRAW_INDEXED=1\n"
                                      "counter frame_bytes.1=64\n"
                                      "counter gl.glClearColor=1\n"
                                      "0 OP op=PASS result=ACCEPTED target=3 color=[0.25,0,0,1] depth=0.33333334 load_color=1\n"
                                      "1   BACKEND op=STATE call=glClearColor link=@0 values=[0.25,0,0,1]\n"
                                      "2 BACKEND op=STATE call=glGenBuffers args=[1,0]\n"
                                      "3 SKIP op=TEXTURE result=CACHE call=glBindTexture key=[7,0]\n"
                                      "4 SKIP op=TEXTURE_SET result=INACTIVE name=99\n"
                                      "5 MARKER_PUSH label=\"frame \\x22a\\x22\\x0a\"\n"
                                      "6   NOTE label=\"mesh\"\n"
                                      "7   NOTE_FIELD key=\"material\" type=U32 value=5\n"
                                      "8   NOTE_FIELD key=\"offset\" type=I32 value=-2\n"
                                      "9   NOTE_FIELD key=\"bits\" type=U64 value=1152921504606846976\n"
                                      "10   NOTE_FIELD key=\"scale\" type=F32 value=-0\n"
                                      "11   NOTE_FIELD key=\"drop\" type=STR value=\"font not loaded\"\n"
                                      "12   OP op=DRAW_INDEXED obj=PIPELINE:65537 result=ACCEPTED first=6 count=3 vertices=4 instances=1 vertex_input=65538\n"
                                      "13   DEFINITION op=CREATE obj=TEXTURE:65539 backend=2 related=[5] width=13 height=7 format=1 levels=1\n"
                                      "14   DEFINITION op=STATE resource=TEXTURE args=[2,11]\n"
                                      "15   DEFINITION op=ATTRIBUTE obj=VERTEX_INPUT:65540 buffer=65541 stride=16 location=1 type=3 count=2\n"
                                      "16   INITIAL op=STATE layer=BACKEND args=[4,0,0,0,640,480]\n"
                                      "17   INITIAL op=STATE layer=FRONTEND integers=[1]\n"
                                      "18   OP op=CLEAR result=ACCEPTED clear_color=[nan:0x7fc00001] clear_depth=0.5 color=1\n"
                                      "19 MARKER_POP\n"
                                      "20   ARGUMENT op=TEXTURE_SET obj=TEXTURE:65539 link=@0 secondary=3 name=42\n"
                                      "21 OP op=UBO obj=BUFFER:65541 slot=1 offset=256 size=64\n"
                                      "22 OP op=DRAW_INDEXED result=CACHE joined=@12 first=9 count=3\n";

static nt_gfx_capture_view_t hand_built_view(nt_gfx_event_t *ev) {
    build_records(ev);
    nt_gfx_capture_view_t view = {.overflow = true, .events = ev, .count = NT_RECORD_COUNT, .execute_begin = 12, .execute_end = 13};
    view.counters.frame_sequence = 9;
    view.counters.vertices = 4;
    view.counters.buffer_upload_bytes = 1ULL << 40;
    view.counters.accepted[NT_GFX_OP_DRAW_INDEXED] = 1;
    view.counters.frame_bytes[1] = 64;
    view.counters.gl[NT_GFX_GL_glClearColor] = 1;
    return view;
}

static void test_every_data_arm_prints_by_name(void) {
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    const nt_gfx_capture_view_t view = hand_built_view(ev);
    char *text = format_all(&view);
    TEST_ASSERT_EQUAL_STRING(k_expected, text);
    free(text);
}

static void test_size_and_truncation(void) {
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    const nt_gfx_capture_view_t view = hand_built_view(ev);
    const size_t needed = strlen(k_expected);
    TEST_ASSERT_EQUAL_UINT64(needed, nt_gfx_capture_format(&view, NULL, 0));
    char small[10];
    memset(small, 'x', sizeof(small));
    TEST_ASSERT_EQUAL_UINT64(needed, nt_gfx_capture_format(&view, small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("ntcapture", small);
}

/* Floats print with '.' whatever the process locale: a comma (de-DE) or a slash (fa-IR) separator. A
   locale the platform lacks is skipped. */
static void test_floats_print_a_point_in_any_locale(void) {
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    const nt_gfx_capture_view_t view = hand_built_view(ev);
    const char *const locales[] = {"de-DE", "de_DE.UTF-8", "fa-IR", "fa_IR.UTF-8"};
    uint32_t tested = 0;
    for (size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        if (setlocale(LC_NUMERIC, locales[i]) != NULL) { // NOLINT(concurrency-mt-unsafe): one test thread
            char *text = format_all(&view);
            TEST_ASSERT_EQUAL_STRING(k_expected, text);
            free(text);
            nt_gfx_event_t parsed[NT_RECORD_COUNT];
            const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(k_expected, strlen(k_expected), parsed, NT_RECORD_COUNT);
            TEST_ASSERT_NULL(r.error);
            text = format_all(&r.view);
            TEST_ASSERT_EQUAL_STRING(k_expected, text);
            free(text);
            tested++;
        }
    }
    (void)setlocale(LC_NUMERIC, "C"); // NOLINT(concurrency-mt-unsafe)
    if (tested == 0) {
        TEST_IGNORE_MESSAGE("no comma or slash locale on this platform");
    }
}

static void test_every_operation_has_a_name(void) {
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        nt_gfx_event_t e = {.kind = NT_GFX_EVENT_OP, .operation = (uint8_t)op};
        const nt_gfx_capture_view_t view = {.events = &e, .count = 1};
        char *text = format_all(&view);
        TEST_ASSERT_NOT_NULL(strstr(text, "0 OP op="));
        free(text);
    }
}

static void test_deep_markers_cap_the_indent(void) {
    enum { DEPTH = 40 };
    nt_gfx_event_t ev[DEPTH + 1];
    memset(ev, 0, sizeof(ev));
    for (int i = 0; i < DEPTH; i++) {
        ev[i].note.kind = NT_GFX_EVENT_MARKER_PUSH;
        ev[i].note.label[0] = 'm';
    }
    ev[DEPTH].note.kind = NT_GFX_EVENT_NOTE;
    const nt_gfx_capture_view_t view = {.events = ev, .count = DEPTH + 1};
    char *text = format_all(&view);
    const char *last = strstr(text, "40 ");
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT64(strlen("40 ") + 64U + strlen("NOTE label=\"\"\n"), strlen(last));
    free(text);
}

static uint32_t count_lines(const char *text, const char *prefix) {
    uint32_t lines = 0;
    for (const char *line = text; *line != '\0'; line = strchr(line, '\n') + 1) {
        lines += prefix == NULL || strncmp(line, prefix, strlen(prefix)) == 0;
    }
    return lines;
}

static void test_a_recorded_frame_prints_one_line_per_record(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = s_program});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_GREATER_THAN_UINT32(0, view.count);
    char *text = format_all(&view);
    TEST_ASSERT_EQUAL_UINT32(2U + count_lines(text, "counter ") + view.count, count_lines(text, NULL));
    free(text);
}

/* Text -> records -> text gives the same bytes. */
static void test_text_round_trips(void) {
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(k_expected, strlen(k_expected), ev, NT_RECORD_COUNT);
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_EQUAL_UINT64(strlen(k_expected), r.next);
    TEST_ASSERT_EQUAL_UINT32(NT_RECORD_COUNT, r.view.count);
    TEST_ASSERT_EQUAL_UINT64(9, r.view.counters.frame_sequence);
    TEST_ASSERT_TRUE(r.view.overflow);
    char *text = format_all(&r.view);
    TEST_ASSERT_EQUAL_STRING(k_expected, text);
    free(text);
}

/* Records -> text -> records gives the same bytes: a field both directions skip would show here. */
static void assert_records_round_trip(const nt_gfx_capture_view_t *view) {
    char *text = format_all(view);
    nt_gfx_event_t *parsed = (nt_gfx_event_t *)calloc(view->count + 1U, sizeof(*parsed));
    TEST_ASSERT_NOT_NULL(parsed);
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text, strlen(text), parsed, view->count);
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_EQUAL_UINT32(view->count, r.view.count);
    TEST_ASSERT_EQUAL_UINT32(view->execute_begin, r.view.execute_begin);
    TEST_ASSERT_EQUAL_UINT32(view->execute_end, r.view.execute_end);
    TEST_ASSERT_EQUAL(view->platform, r.view.platform);
    TEST_ASSERT_EQUAL_MEMORY(view->events, parsed, (size_t)view->count * sizeof(*parsed));
    TEST_ASSERT_EQUAL_MEMORY(&view->counters, &r.view.counters, sizeof(view->counters));
    free(parsed);
    free(text);
}

static void test_a_recorded_frame_round_trips(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    NT_GFX_MARKER_PUSH("scene");
    NT_GFX_NOTE("mesh", NT_NOTE_U32("material", 3), NT_NOTE_STR("drop", "none"));
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = s_program});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_end_pass();
    NT_GFX_MARKER_POP();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_GREATER_THAN_UINT32(0, view.count);
    assert_records_round_trip(&view);
}

/* Extremes of every scalar kind survive both directions. */
static void test_extremes_round_trip(void) {
    nt_gfx_event_t ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_OP;
    ev[0].operation = NT_GFX_OP_CLEAR;
    ev[0].data.clear.clear_color[0] = float_from_bits(1U);          /* the smallest subnormal */
    ev[0].data.clear.clear_color[1] = float_from_bits(0x7f800000U); /* inf */
    ev[0].data.clear.clear_color[2] = float_from_bits(0xff800000U); /* -inf */
    ev[0].data.clear.clear_color[3] = float_from_bits(0x7f7fffffU); /* FLT_MAX */
    ev[0].data.clear.clear_depth = float_from_bits(0xffc00123U);    /* a negative NaN with payload */
    ev[1].kind = NT_GFX_EVENT_OP;
    ev[1].operation = NT_GFX_OP_BUFFER_UPLOAD;
    ev[1].object_kind = NT_GFX_OBJECT_BUFFER;
    ev[1].object = UINT32_MAX;
    ev[1].data.resource.size = UINT64_MAX;
    ev[1].data.resource.related[2] = UINT32_MAX;
    ev[2].note.kind = NT_GFX_EVENT_NOTE;
    memset(ev[2].note.label, 'x', sizeof(ev[2].note.label) - 1U); /* the longest label */
    const nt_gfx_capture_view_t view = {.events = ev, .count = 3, .platform = NT_GFX_CAPTURE_WEB};
    assert_records_round_trip(&view);
}

static void test_documents_parse_back_to_back(void) {
    const char *bad = "ntcapture 2\nframe sequence=1 platform=native records=2 overflow=0 execute=0..0\n0 OP op=PASS\n1 OP op=NO_SUCH_OP\n";
    const char *web_head = "ntcapture 2\nframe sequence=9 platform=web";
    const char *tail = strstr(k_expected, " records="); /* the same records, recorded on the other platform */
    const size_t len = strlen(k_expected) + strlen(bad) + strlen(web_head) + strlen(tail);
    char *text = (char *)malloc(len + 1U);
    TEST_ASSERT_NOT_NULL(text);
    (void)snprintf(text, len + 1U, "%s%s%s%s", k_expected, bad, web_head, tail);
    nt_gfx_event_t ev[(NT_RECORD_COUNT * 2) + 2];
    size_t at = 0;
    uint32_t used = 0;
    const char *errors[3] = {NULL, NULL, NULL};
    uint32_t counts[3] = {0};
    uint32_t documents = 0;
    while (at < strlen(text) && documents < 3) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text + at, strlen(text) - at, ev + used, (uint32_t)(sizeof(ev) / sizeof(ev[0])) - used);
        errors[documents] = r.error;
        counts[documents] = r.view.count;
        if (documents == 2) {
            TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_WEB, r.view.platform);
        }
        used += r.view.count;
        at += r.next;
        documents++;
    }
    TEST_ASSERT_EQUAL_UINT32(3, documents);
    TEST_ASSERT_EQUAL_UINT64(strlen(text), at);
    TEST_ASSERT_NULL(errors[0]);
    TEST_ASSERT_NOT_NULL(errors[1]);
    TEST_ASSERT_NULL(errors[2]);
    TEST_ASSERT_EQUAL_UINT32(NT_RECORD_COUNT, counts[0]);
    TEST_ASSERT_EQUAL_UINT32(1, counts[1]); /* the valid prefix before the unknown name */
    TEST_ASSERT_EQUAL_UINT32(NT_RECORD_COUNT, counts[2]);
    free(text);
}

#define NT_HEADER_ "ntcapture 2\nframe sequence=1 platform=native records=2 overflow=0 execute=0..0\n"

/* Each outcome: rejected (NULL events) or the records kept, and where the error points (`at`: its first
 * occurrence in the text; NULL: the end of the text). */
static void test_bad_text_reports_where_and_keeps_a_prefix(void) {
    static const struct {
        const char *text;
        const char *at;
        uint32_t skip; /* bytes from the start of `at` to the error */
        int32_t kept;  /* -1: the document is rejected */
        uint32_t cap;
    } rows[] = {
        {"ntcapture 1\n", "1\n", 0, -1, 4},
        {"nt\n", "nt", 0, -1, 4},
        {NT_HEADER_ "counter bogus=1\n0 OP op=PASS\n1 OP op=PASS\n", "bogus", 0, -1, 4},
        {"ntcapture 2\nframe sequence=1 platform=native records=1 overflow=0 execute=0..2\n0 OP op=PASS\n", "\n0 OP", 0, -1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=NO_SUCH_OP\n", "NO_SUCH", 0, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n", NULL, 0, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS", "1 OP", 0, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS\n2 OP op=PASS\n", "2 OP", 0, 2, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS\n", "1 OP", 0, 1, 1},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS link=@1\n", "@1", 1, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=DRAW joined=@0\n", "@0", 1, 1, 4},
        {NT_HEADER_ "0 OP op=PASS load_color=2\n", "=2\n", 1, 0, 4},
        {NT_HEADER_ "0 OP op=SCISSOR integers=[4294967296]\n", "4294967296", 0, 0, 4},
        {NT_HEADER_ "0 OP op=PASS\r\n", "\r", 0, 0, 4},
        {NT_HEADER_ "0 BACKEND op=STATE args=[1]\n", " args", 0, 0, 4},
        {NT_HEADER_ "0 NOTE_FIELD key=\"k\" type=U32 value=1\n", " key", 0, 0, 4},
        {NT_HEADER_ "0 NOTE label=\"a\\x00\"\n", "\\x00", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=0x1p3\n", "0x1p3", 0, 0, 4},
        {NT_HEADER_ "1 OP op=PASS\n", "1 OP", 0, 0, 4},
        {NT_HEADER_ "0 OP op=SCISSOR integers=[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]\n", "15]", 0, 0, 4},
        {NT_HEADER_ "0 NOTE label=\"n\"\n1 NOTE_FIELD key=\"AAAAAAAAAAAAAAAAAAAAAAAX\" type=U32 value=1\n", "X\"", 0, 1, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=nan:0x7f800000\n", "nan:", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=1..5\n", "1..5", 0, 0, 4},
        {NT_HEADER_ "counter frame_bytes.99=5\n0 OP op=PASS\n1 OP op=PASS\n", "99=", 2, -1, 4},
        {"ntcapture 2\nframe sequence=1 platform=native records=0 overflow=2 execute=0..0\n", "2 exec", 0, -1, 4},
        {"ntcapture 2\nframe sequence=1 platform=mars records=0 overflow=0 execute=0..0\n", "mars", 0, -1, 4},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        nt_gfx_event_t ev[4];
        const size_t len = strlen(rows[i].text);
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(rows[i].text, len, ev, rows[i].cap);
        TEST_ASSERT_NOT_NULL_MESSAGE(r.error, rows[i].text);
        const size_t expected_at = rows[i].at != NULL ? (size_t)(strstr(rows[i].text, rows[i].at) - rows[i].text) + rows[i].skip : len;
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(expected_at, r.error_at, rows[i].text);
        if (rows[i].kept < 0) {
            TEST_ASSERT_NULL_MESSAGE(r.view.events, rows[i].text);
        } else {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)rows[i].kept, r.view.count, rows[i].text);
        }
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(len, r.next, rows[i].text);
    }
    /* A valid prefix keeps the execution range inside its records. */
    const char *cut = "ntcapture 2\nframe sequence=1 platform=native records=3 overflow=0 execute=1..3\n0 OP op=PASS\n1 OP op=PASS\n2 OP op=NO\n";
    nt_gfx_event_t ev[3];
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(cut, strlen(cut), ev, 3);
    TEST_ASSERT_EQUAL_UINT32(2, r.view.count);
    TEST_ASSERT_EQUAL_UINT32(1, r.view.execute_begin);
    TEST_ASSERT_EQUAL_UINT32(2, r.view.execute_end);
}

/* Real frames saved by gfx_demo from the web host: each document parses whole and formats to its own bytes on native. */
static void test_a_saved_web_file_round_trips(void) {
    FILE *f = fopen(NT_CAPTURE_FIXTURE, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, NT_CAPTURE_FIXTURE);
    char *text = (char *)malloc(1U << 20);
    TEST_ASSERT_NOT_NULL(text);
    const size_t len = fread(text, 1, 1U << 20, f);
    (void)fclose(f);
    TEST_ASSERT_LESS_THAN_UINT64(1U << 20, len);
    uint32_t lines = 0;
    for (size_t i = 0; i < len; i++) {
        lines += text[i] == '\n';
    }
    nt_gfx_event_t *events = (nt_gfx_event_t *)calloc((size_t)lines + 1U, sizeof(*events));
    TEST_ASSERT_NOT_NULL(events);
    uint32_t used = 0;
    uint32_t documents = 0;
    for (size_t at = 0; at < len; documents++) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text + at, len - at, events + used, lines - used);
        TEST_ASSERT_NULL_MESSAGE(r.error, r.error);
        TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_WEB, r.view.platform);
        char *again = format_all(&r.view);
        TEST_ASSERT_EQUAL_UINT64(r.next, strlen(again));
        TEST_ASSERT_EQUAL_MEMORY(text + at, again, r.next);
        free(again);
        used += r.view.count;
        at += r.next;
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, documents);
    free(events);
    free(text);
}

/* The contract for untrusted text: any cut or damaged byte parses without asserting, and whatever is kept formats. */
static void test_damaged_text_never_asserts(void) {
    const size_t len = strlen(k_expected);
    char *text = (char *)malloc(len + 1U);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    static const char k_bytes[] = {'0', '9', '@', ' ', '"', '\\', 'x', '\n', 'A', '-', '[', ',', '=', '.'};
    for (size_t cut = 0; cut <= len; cut++) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(k_expected, cut, ev, NT_RECORD_COUNT);
        if (r.view.events != NULL) {
            free(format_all(&r.view));
        }
    }
    for (size_t at = 0; at < len; at++) {
        for (size_t b = 0; b < sizeof(k_bytes); b++) {
            memcpy(text, k_expected, len + 1U);
            text[at] = k_bytes[b];
            const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text, len, ev, NT_RECORD_COUNT);
            if (r.view.events != NULL) {
                free(format_all(&r.view));
            }
        }
    }
    free(text);
}
#endif

int main(void) {
    UNITY_BEGIN();
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_every_data_arm_prints_by_name);
    RUN_TEST(test_size_and_truncation);
    RUN_TEST(test_floats_print_a_point_in_any_locale);
    RUN_TEST(test_every_operation_has_a_name);
    RUN_TEST(test_deep_markers_cap_the_indent);
    RUN_TEST(test_a_recorded_frame_prints_one_line_per_record);
    RUN_TEST(test_text_round_trips);
    RUN_TEST(test_a_recorded_frame_round_trips);
    RUN_TEST(test_extremes_round_trip);
    RUN_TEST(test_documents_parse_back_to_back);
    RUN_TEST(test_bad_text_reports_where_and_keeps_a_prefix);
    RUN_TEST(test_a_saved_web_file_round_trips);
    RUN_TEST(test_damaged_text_never_asserts);
#endif
    return UNITY_END();
}
