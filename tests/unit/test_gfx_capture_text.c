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
    desc.capture_string_capacity = 16384;
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

/* Strings of hand-built views, in record order as gfx appends them; [0] is the "" of offset 0. */
static char s_strings[4096];
static uint32_t s_string_bytes;

static nt_gfx_capture_string_t add_string(const char *src) {
    const uint32_t at = s_string_bytes;
    memcpy(s_strings + at, src, strlen(src) + 1);
    s_string_bytes += (uint32_t)strlen(src) + 1U;
    return at;
}

/* Parsed strings: the document's own (one file's documents share it), then a second parse's. */
static char s_parsed[1U << 20];
static char s_again[1U << 20];

static float float_from_bits(uint32_t bits) {
    float f = 0.0F;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

#define NT_RECORD_COUNT 25

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size) -- one record per data arm
static void build_records(nt_gfx_event_t *ev) {
    memset(ev, 0, NT_RECORD_COUNT * sizeof(*ev));
    s_string_bytes = 1;
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
    ev[3] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_SKIP, .operation = NT_GFX_OP_TEXTURE, .result = NT_GFX_RESULT_CACHE, .detail = NT_GFX_GL_glBindTexture};
    ev[3].data.backend.args[0] = 7;
    ev[4] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_SKIP, .operation = NT_GFX_OP_TEXTURE_SET, .result = NT_GFX_RESULT_INACTIVE};
    ev[4].data.texture_binding.name = 99;
    ev[5].note.kind = NT_GFX_EVENT_MARKER_PUSH;
    ev[5].note.label = add_string("frame \"a\"\n");
    ev[6].note.kind = NT_GFX_EVENT_NOTE;
    ev[6].note.label = add_string("mesh");
    const nt_gfx_note_type_t types[5] = {NT_GFX_NOTE_U32, NT_GFX_NOTE_I32, NT_GFX_NOTE_U64, NT_GFX_NOTE_F32, NT_GFX_NOTE_STR};
    const char *keys[5] = {"material", "offset", "bits", "scale", "drop"};
    for (int i = 0; i < 5; i++) {
        ev[7 + i].field.kind = NT_GFX_EVENT_NOTE_FIELD;
        ev[7 + i].field.type = types[i];
        ev[7 + i].field.key = add_string(keys[i]);
    }
    ev[7].field.value.u32 = 5;
    ev[8].field.value.i32 = -2;
    ev[9].field.value.u64 = 1ULL << 60;
    ev[10].field.value.f32 = -0.0F;
    ev[11].field.value.str = add_string("font not loaded");
    ev[12] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_DRAW_INDEXED, .object_kind = NT_GFX_OBJECT_PIPELINE, .object = 65537, .result = NT_GFX_RESULT_ACCEPTED};
    ev[12].data.draw.first = 6;
    ev[12].data.draw.count = 3;
    ev[12].data.draw.vertices = 4;
    ev[12].data.draw.instances = 1;
    ev[12].data.draw.vertex_input = 65538;
    ev[13] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 65539};
    ev[13].data.texture.slot = 2;
    ev[13].data.texture.sampler = 5;
    ev[13].data.texture.width = 13;
    ev[13].data.texture.height = 7;
    ev[13].data.texture.format = 1;
    ev[13].data.texture.levels = 1;
    ev[13].data.texture.label = add_string("albedo");
    ev[14] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_OBJECT_TEXTURE};
    ev[14].data.backend_resource.slot = 2;
    ev[14].data.backend_resource.gl_name = 11;
    ev[15] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_ATTRIBUTE, .object_kind = NT_GFX_OBJECT_VERTEX_INPUT, .object = 65540};
    ev[15].data.attribute.buffer = 65541;
    ev[15].data.attribute.stride = 16;
    ev[15].data.attribute.location = 1;
    ev[15].data.attribute.type = 3;
    ev[15].data.attribute.count = 2;
    ev[16] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_INITIAL, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_INITIAL_BACKEND};
    ev[16].data.backend_state.program = 4;
    ev[16].data.backend_state.width = 640;
    ev[16].data.backend_state.height = 480;
    ev[17] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_INITIAL, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_INITIAL_FRONTEND};
    ev[17].data.frontend.context_lost = true;
    ev[18] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_CLEAR, .result = NT_GFX_RESULT_ACCEPTED};
    ev[18].data.clear.clear_color[0] = float_from_bits(0x7fc00001U);
    ev[18].data.clear.clear_depth = 0.5F;
    ev[18].data.clear.color = true;
    ev[19].kind = NT_GFX_EVENT_MARKER_POP;
    ev[20] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_ARGUMENT, .operation = NT_GFX_OP_TEXTURE_SET, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 65539, .link = 1};
    ev[20].data.texture_binding.sampler = 3;
    ev[20].data.texture_binding.name = 42;
    ev[21] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_UBO, .object_kind = NT_GFX_OBJECT_BUFFER, .object = 65541};
    ev[21].data.ubo.slot = 1;
    ev[21].data.ubo.offset = 256;
    ev[21].data.ubo.size = 64;
    ev[22] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_DRAW_INDEXED, .result = NT_GFX_RESULT_CACHE, .detail = 13};
    ev[22].data.draw.first = 9;
    ev[22].data.draw.count = 3;
    ev[23].note.kind = NT_GFX_EVENT_NOTE;
    ev[23].note.label = add_string("sprite");
    ev[24].field.kind = NT_GFX_EVENT_NOTE_FIELD;
    ev[24].field.type = NT_GFX_NOTE_HANDLE;
    ev[24].field.key = add_string("page");
    ev[24].field.value.handle.kind = NT_GFX_OBJECT_TEXTURE;
    ev[24].field.value.handle.id = 65539;
}

static const char *const k_expected = "ntcapture 4\n"
                                      "frame sequence=9 platform=native records=25 overflow=1 execute=12..13\n"
                                      "counter vertices=4\n"
                                      "counter buffer_upload_bytes=1099511627776\n"
                                      "counter accepted.DRAW_INDEXED=1\n"
                                      "counter frame_bytes.1=64\n"
                                      "counter gl.glClearColor=1\n"
                                      "0 OP op=PASS result=ACCEPTED target=3 color=[0.25,0,0,1] depth=0.33333334 load_color=1\n"
                                      "1   BACKEND op=STATE call=glClearColor link=@0 values=[0.25,0,0,1]\n"
                                      "2 BACKEND op=STATE call=glGenBuffers args=[1,0]\n"
                                      "3 SKIP op=TEXTURE result=CACHE call=glBindTexture texture=7\n"
                                      "4 SKIP op=TEXTURE_SET result=INACTIVE name=99\n"
                                      "5 MARKER_PUSH label=\"frame \\x22a\\x22\\x0a\"\n"
                                      "6   NOTE label=\"mesh\"\n"
                                      "7   NOTE_FIELD key=\"material\" type=U32 value=5\n"
                                      "8   NOTE_FIELD key=\"offset\" type=I32 value=-2\n"
                                      "9   NOTE_FIELD key=\"bits\" type=U64 value=1152921504606846976\n"
                                      "10   NOTE_FIELD key=\"scale\" type=F32 value=-0\n"
                                      "11   NOTE_FIELD key=\"drop\" type=STR value=\"font not loaded\"\n"
                                      "12   OP op=DRAW_INDEXED obj=PIPELINE:65537 result=ACCEPTED first=6 count=3 vertices=4 instances=1 vertex_input=65538\n"
                                      "13   DEFINITION op=CREATE obj=TEXTURE:65539 slot=2 width=13 height=7 format=1 levels=1 sampler=5 label=\"albedo\"\n"
                                      "14   DEFINITION op=STATE resource=TEXTURE slot=2 gl_name=11\n"
                                      "15   DEFINITION op=ATTRIBUTE obj=VERTEX_INPUT:65540 buffer=65541 stride=16 location=1 type=3 count=2\n"
                                      "16   INITIAL op=STATE layer=BACKEND program=4 width=640 height=480\n"
                                      "17   INITIAL op=STATE layer=FRONTEND context_lost=1\n"
                                      "18   OP op=CLEAR result=ACCEPTED clear_color=[nan:0x7fc00001] clear_depth=0.5 color=1\n"
                                      "19 MARKER_POP\n"
                                      "20   ARGUMENT op=TEXTURE_SET obj=TEXTURE:65539 link=@0 name=42 sampler=3\n"
                                      "21 OP op=UBO obj=BUFFER:65541 slot=1 offset=256 size=64\n"
                                      "22 OP op=DRAW_INDEXED result=CACHE joined=@12 first=9 count=3\n"
                                      "23 NOTE label=\"sprite\"\n"
                                      "24 NOTE_FIELD key=\"page\" type=HANDLE value=TEXTURE:65539\n";

static nt_gfx_capture_view_t hand_built_view(nt_gfx_event_t *ev) {
    build_records(ev);
    nt_gfx_capture_view_t view = {.overflow = true, .events = ev, .count = NT_RECORD_COUNT, .execute_begin = 12, .execute_end = 13, .strings = s_strings, .string_bytes = s_string_bytes};
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
        ev[i].note.label = 1;
    }
    ev[DEPTH].note.kind = NT_GFX_EVENT_NOTE;
    const nt_gfx_capture_view_t view = {.events = ev, .count = DEPTH + 1, .strings = "\0m", .string_bytes = 3};
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
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(k_expected, strlen(k_expected), ev, NT_RECORD_COUNT, s_parsed, sizeof(s_parsed));
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
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text, strlen(text), parsed, view->count, s_parsed, sizeof(s_parsed));
    TEST_ASSERT_NULL(r.error);
    TEST_ASSERT_EQUAL_UINT32(view->count, r.view.count);
    TEST_ASSERT_EQUAL_UINT32(view->string_bytes, r.view.string_bytes);
    TEST_ASSERT_EQUAL_MEMORY(view->strings, r.view.strings, view->string_bytes);
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
    nt_gfx_event_t ev[4];
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
    ev[1].data.upload.size = UINT64_MAX;
    ev[1].data.upload.offset = UINT32_MAX;
    ev[2].note.kind = NT_GFX_EVENT_NOTE;
    static char strings[302];
    memset(strings + 1, 'x', 300); /* strings have no length limit */
    ev[2].note.label = 1;
    ev[3].kind = NT_GFX_EVENT_OP;
    ev[3].operation = NT_GFX_OP_SCISSOR;
    ev[3].data.rect.x = INT32_MIN;
    ev[3].data.rect.y = INT32_MAX;
    ev[3].data.rect.width = -1;
    const nt_gfx_capture_view_t view = {.events = ev, .count = 4, .platform = NT_GFX_CAPTURE_WEB, .strings = strings, .string_bytes = sizeof(strings)};
    assert_records_round_trip(&view);
}

/* Issued-call argument counts, cache keys, joined draws and note fields come back byte for byte. */
static void test_every_arm_round_trips(void) {
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    const nt_gfx_capture_view_t view = hand_built_view(ev);
    assert_records_round_trip(&view);
}

/* A value for every field of a record's shape: nonzero, so the text prints it, and distinct per field. */
static void fill_fields(nt_gfx_event_t *e, uint32_t seed) {
    const nt_gfx_capture_field_t *fields = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &fields);
    unsigned char *base = (unsigned char *)e;
    for (uint32_t f = 0; f < n; f++) {
        unsigned char *at = base + fields[f].offset;
        const uint32_t v = seed + f + 1U;
        switch ((nt_gfx_field_type_t)fields[f].type) {
        case NT_GFX_FIELD_U32:
            memcpy(at, &v, sizeof(v));
            break;
        case NT_GFX_FIELD_I32:
            memcpy(at, &(int32_t){-(int32_t)v}, sizeof(int32_t));
            break;
        case NT_GFX_FIELD_U64:
            memcpy(at, &(uint64_t){((uint64_t)v << 33) + 1U}, sizeof(uint64_t));
            break;
        case NT_GFX_FIELD_F32:
            memcpy(at, &(float){(float)v + 0.5F}, sizeof(float));
            break;
        case NT_GFX_FIELD_BOOL:
            *(bool *)at = true;
            break;
        case NT_GFX_FIELD_U32S:
        case NT_GFX_FIELD_F32S:
            for (uint32_t k = 0; k < fields[f].size; k++) {
                uint32_t bits = v + k;
                if (fields[f].type == NT_GFX_FIELD_F32S) {
                    const float real = (float)bits;
                    memcpy(&bits, &real, sizeof(bits));
                }
                memcpy(at + ((size_t)k * sizeof(bits)), &bits, sizeof(bits));
            }
            break;
        case NT_GFX_FIELD_STR:
            memcpy(at, &(nt_gfx_capture_string_t){add_string("s")}, sizeof(nt_gfx_capture_string_t));
            break;
        }
    }
    if (e->kind == NT_GFX_EVENT_BACKEND) {
        e->arg_count = (uint8_t)(12U | (4U << 4));
    }
}

/* No byte of a record belongs to two of its fields. */
static void assert_fields_disjoint(const nt_gfx_event_t *e) {
    uint8_t owner[sizeof(nt_gfx_event_t)] = {0};
    const nt_gfx_capture_field_t *fields = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &fields);
    for (uint32_t f = 0; f < n; f++) {
        size_t width = (size_t)fields[f].size * 4U;
        if (fields[f].type == NT_GFX_FIELD_U64) {
            width = 8U;
        } else if (fields[f].type == NT_GFX_FIELD_BOOL) {
            width = 1U;
        }
        for (size_t b = fields[f].offset; b < fields[f].offset + width; b++) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, owner[b], fields[f].name);
            owner[b] = (uint8_t)(f + 1U);
        }
    }
}

/* A record header the text can name: detail 0 except a call (one valid call), a resource kind or a layer. */
static bool header_names(nt_gfx_event_t *e, uint32_t detail) {
    const bool call = e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP;
    const bool resource = e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_STATE;
    const bool layer = e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_STATE;
    if (call) {
        e->detail = detail == 1 ? (uint32_t)NT_GFX_GL_glBindTexture : 0U;
        return detail <= 1 && (e->kind == NT_GFX_EVENT_SKIP || e->detail != 0); /* an issued call names its call */
    }
    e->detail = detail;
    return detail == 0 || resource || (layer && detail <= NT_GFX_INITIAL_BACKEND);
}

/* Every record the field table describes, with every field set: the table's offsets fit the record and both
 * directions read and write each field. */
static void test_every_shape_round_trips(void) {
    enum { MAX_RECORDS = 4096 };
    nt_gfx_event_t *ev = (nt_gfx_event_t *)calloc(MAX_RECORDS, sizeof(*ev));
    TEST_ASSERT_NOT_NULL(ev);
    const nt_gfx_event_kind_t kinds[] = {NT_GFX_EVENT_OP, NT_GFX_EVENT_BACKEND, NT_GFX_EVENT_SKIP, NT_GFX_EVENT_INITIAL, NT_GFX_EVENT_DEFINITION, NT_GFX_EVENT_ARGUMENT};
    uint32_t count = 0;
    s_string_bytes = 1;
    for (uint32_t s = 0; s < (uint32_t)(sizeof(kinds) / sizeof(kinds[0])) * NT_GFX_OP_COUNT * 100U; s++) {
        const uint32_t object = s % 10U;
        const uint32_t detail = (s / 10U) % 10U;
        nt_gfx_event_t e = {.kind = kinds[s / (NT_GFX_OP_COUNT * 100U)], .operation = (uint8_t)((s / 100U) % NT_GFX_OP_COUNT), .object_kind = (uint8_t)object, .object = object != 0 ? 65537U : 0U};
        const nt_gfx_capture_field_t *fields = NULL;
        if (object > NT_GFX_OBJECT_INSTANCE_LAYOUT || detail > NT_GFX_OBJECT_INSTANCE_LAYOUT || !header_names(&e, detail) || nt_gfx_capture_fields(&e, &fields) == 0) {
            continue;
        }
        TEST_ASSERT_LESS_THAN_UINT32(MAX_RECORDS, count);
        assert_fields_disjoint(&e);
        fill_fields(&e, count * 16U);
        ev[count++] = e;
    }
    TEST_ASSERT_GREATER_THAN_UINT32(30, count);
    const nt_gfx_capture_view_t view = {.events = ev, .count = count, .strings = s_strings, .string_bytes = s_string_bytes};
    assert_records_round_trip(&view);
    free(ev);
}

/* Writers fill the fields their records print: a recorded frame formats (no byte outside a record's fields), and
 * the unpacked sampler, the texture and buffer requests and a signed rectangle hold the values passed. */
static void test_writers_fill_their_fields(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const nt_sampler_desc_t sd = {.min_filter = NT_FILTER_LINEAR_MIPMAP_NEAREST, .mag_filter = NT_FILTER_LINEAR, .wrap_u = NT_WRAP_REPEAT, .wrap_v = NT_WRAP_MIRRORED_REPEAT};
    const nt_sampler_t smp = nt_gfx_make_sampler(&sd);
    const uint8_t pixel[4] = {0};
    (void)nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = pixel, .format = NT_TEXTURE_FORMAT_RGBA8, .gen_mipmaps = true});
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_STREAM, .size = 16, .index_type = NT_INDEX_UINT16});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_set_scissor(-3, 4, 5, 6);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const nt_gfx_capture_view_t view = nt_gfx_capture_read();
    free(format_all(&view));
    uint32_t seen = 0;
    for (uint32_t i = 0; i < view.count; i++) {
        const nt_gfx_event_t *e = &view.events[i];
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_CREATE && e->object_kind == NT_GFX_OBJECT_SAMPLER && e->object == smp.id) {
            TEST_ASSERT_EQUAL_UINT32(NT_FILTER_LINEAR_MIPMAP_NEAREST, e->data.sampler.min_filter);
            TEST_ASSERT_EQUAL_UINT32(NT_FILTER_LINEAR, e->data.sampler.mag_filter);
            TEST_ASSERT_EQUAL_UINT32(NT_WRAP_REPEAT, e->data.sampler.wrap_u);
            TEST_ASSERT_EQUAL_UINT32(NT_WRAP_MIRRORED_REPEAT, e->data.sampler.wrap_v);
            seen |= 1U;
        } else if (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_CREATE && e->object_kind == NT_GFX_OBJECT_TEXTURE) {
            TEST_ASSERT_TRUE(e->data.texture.gen_mipmaps);
            TEST_ASSERT_EQUAL_UINT32(NT_TEXTURE_FORMAT_RGBA8, e->data.texture.format);
            seen |= 2U;
        } else if (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_CREATE && e->object_kind == NT_GFX_OBJECT_BUFFER) {
            TEST_ASSERT_EQUAL_UINT32(NT_BUFFER_INDEX, e->data.buffer.type);
            TEST_ASSERT_EQUAL_UINT32(NT_USAGE_STREAM, e->data.buffer.usage);
            TEST_ASSERT_EQUAL_UINT32(NT_INDEX_UINT16, e->data.buffer.index_type);
            TEST_ASSERT_FALSE(e->data.buffer.data);
            seen |= 4U;
        } else if (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_SCISSOR) {
            TEST_ASSERT_EQUAL_INT32(-3, e->data.rect.x);
            TEST_ASSERT_EQUAL_INT32(6, e->data.rect.height);
            seen |= 8U;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(15U, seen);
}

static void test_documents_parse_back_to_back(void) {
    const char *bad = "ntcapture 4\nframe sequence=1 platform=native records=2 overflow=0 execute=0..0\n0 OP op=PASS\n1 OP op=NO_SUCH_OP\n";
    const char *web_head = "ntcapture 4\nframe sequence=9 platform=web";
    const char *tail = strstr(k_expected, " records="); /* the same records, recorded on the other platform */
    const size_t len = strlen(k_expected) + strlen(bad) + strlen(web_head) + strlen(tail);
    char *text = (char *)malloc(len + 1U);
    TEST_ASSERT_NOT_NULL(text);
    (void)snprintf(text, len + 1U, "%s%s%s%s", k_expected, bad, web_head, tail);
    nt_gfx_event_t ev[(NT_RECORD_COUNT * 2) + 2];
    size_t at = 0;
    uint32_t used = 0;
    uint32_t string_used = 0;
    const char *errors[3] = {NULL, NULL, NULL};
    uint32_t counts[3] = {0};
    uint32_t documents = 0;
    while (at < strlen(text) && documents < 3) {
        const nt_gfx_capture_parsed_t r =
            nt_gfx_capture_parse(text + at, strlen(text) - at, ev + used, (uint32_t)(sizeof(ev) / sizeof(ev[0])) - used, s_parsed + string_used, (uint32_t)sizeof(s_parsed) - string_used);
        errors[documents] = r.error;
        counts[documents] = r.view.count;
        if (documents == 2) {
            TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_WEB, r.view.platform);
        }
        used += r.view.count;
        string_used += r.view.string_bytes;
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

#define NT_HEADER_ "ntcapture 4\nframe sequence=1 platform=native records=2 overflow=0 execute=0..0\n"

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
        {"ntcapture 3\n", "3\n", 0, -1, 4}, /* the record layout changed in 4: no older version reads */
        {"nt\n", "nt", 0, -1, 4},
        {NT_HEADER_ "counter bogus=1\n0 OP op=PASS\n1 OP op=PASS\n", "bogus", 0, -1, 4},
        {"ntcapture 4\nframe sequence=1 platform=native records=1 overflow=0 execute=0..2\n0 OP op=PASS\n", "\n0 OP", 0, -1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=NO_SUCH_OP\n", "NO_SUCH", 0, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n", NULL, 0, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS", "1 OP", 0, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS\n2 OP op=PASS\n", "2 OP", 0, 2, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS\n", "1 OP", 0, 1, 1},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=PASS link=@1\n", "@1", 1, 1, 4},
        {NT_HEADER_ "0 OP op=PASS\n1 OP op=DRAW joined=@0\n", "@0", 1, 1, 4},
        {NT_HEADER_ "0 OP op=PASS load_color=2\n", "=2\n", 1, 0, 4},
        {NT_HEADER_ "0 OP op=SCISSOR x=2147483648\n", "2147483648", 0, 0, 4},
        {NT_HEADER_ "0 OP op=SCISSOR x=-2147483649\n", "2147483649", 0, 0, 4},
        {NT_HEADER_ "0 NOTE label=\"a\"\n1 NOTE_FIELD key=\"k\" type=HANDLE value=NOPE:1\n", "NOPE", 0, 1, 4},
        {NT_HEADER_ "0 NOTE label=\"a\"\n1 NOTE_FIELD key=\"k\" type=HANDLE value=NONE:1\n", ":1", 0, 1, 4}, /* gfx never records a handle of no kind */
        {NT_HEADER_ "0 OP op=PASS\r\n", "\r", 0, 0, 4},
        {NT_HEADER_ "0 BACKEND op=STATE args=[1]\n", " args", 0, 0, 4},
        {NT_HEADER_ "0 NOTE_FIELD key=\"k\" type=U32 value=1\n", " key", 0, 0, 4},
        {NT_HEADER_ "0 NOTE label=\"a\\x00\"\n", "\\x00", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=0x1p3\n", "0x1p3", 0, 0, 4},
        {NT_HEADER_ "1 OP op=PASS\n", "1 OP", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_color=[1,2,3,4,5]\n", "5]", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=nan:0x7f800000\n", "nan:", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=1..5\n", "1..5", 0, 0, 4},
        {NT_HEADER_ "0 OP op=CLEAR clear_depth=1e100\n", "1e100", 0, 0, 4},
        {NT_HEADER_ "counter frame_bytes.99=5\n0 OP op=PASS\n1 OP op=PASS\n", "99=", 2, -1, 4},
        {"ntcapture 4\nframe sequence=1 platform=native records=0 overflow=2 execute=0..0\n", "2 exec", 0, -1, 4},
        {"ntcapture 4\nframe sequence=1 platform=mars records=0 overflow=0 execute=0..0\n", "mars", 0, -1, 4},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        nt_gfx_event_t ev[4];
        const size_t len = strlen(rows[i].text);
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(rows[i].text, len, ev, rows[i].cap, s_parsed, sizeof(s_parsed));
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
    const char *cut = "ntcapture 4\nframe sequence=1 platform=native records=3 overflow=0 execute=1..3\n0 OP op=PASS\n1 OP op=PASS\n2 OP op=NO\n";
    nt_gfx_event_t ev[3];
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(cut, strlen(cut), ev, 3, s_parsed, sizeof(s_parsed));
    TEST_ASSERT_EQUAL_UINT32(2, r.view.count);
    TEST_ASSERT_EQUAL_UINT32(1, r.view.execute_begin);
    TEST_ASSERT_EQUAL_UINT32(2, r.view.execute_end);
}

/* Strings that run past the array end the document like records do: the lines before keep their strings, the
 * failed line's are dropped. */
static void test_strings_past_the_array_keep_a_prefix(void) {
    const char *text = NT_HEADER_ "0 NOTE label=\"ab\"\n1 NOTE label=\"cdef\"\n";
    nt_gfx_event_t ev[2];
    char strings[6];
    const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text, strlen(text), ev, 2, strings, sizeof(strings));
    TEST_ASSERT_NOT_NULL(r.error);
    TEST_ASSERT_EQUAL_UINT64((size_t)(strstr(text, "cdef") - text) + 1U, r.error_at); /* only "c" and its NUL fit beside "ab" */
    TEST_ASSERT_EQUAL_UINT32(1, r.view.count);
    TEST_ASSERT_EQUAL_UINT32(4, r.view.string_bytes);
    TEST_ASSERT_EQUAL_STRING("ab", r.view.strings + ev[0].note.label);

    /* A line whose first string fits and second does not drops both. */
    const char *field = NT_HEADER_ "0 NOTE label=\"ab\"\n1 NOTE_FIELD key=\"k\" type=STR value=\"long\"\n";
    const nt_gfx_capture_parsed_t f = nt_gfx_capture_parse(field, strlen(field), ev, 2, strings, sizeof(strings));
    TEST_ASSERT_NOT_NULL(f.error);
    TEST_ASSERT_EQUAL_UINT32(1, f.view.count);
    TEST_ASSERT_EQUAL_UINT32(4, f.view.string_bytes);
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
    uint32_t string_used = 0;
    uint32_t documents = 0;
    for (size_t at = 0; at < len; documents++) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text + at, len - at, events + used, lines - used, s_parsed + string_used, (uint32_t)sizeof(s_parsed) - string_used);
        TEST_ASSERT_NULL_MESSAGE(r.error, r.error);
        TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_WEB, r.view.platform);
        char *again = format_all(&r.view);
        TEST_ASSERT_EQUAL_UINT64(r.next, strlen(again));
        TEST_ASSERT_EQUAL_MEMORY(text + at, again, r.next);
        free(again);
        used += r.view.count;
        string_used += r.view.string_bytes;
        at += r.next;
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, documents);
    free(events);
    free(text);
}

/* The contract for untrusted text: any cut or damaged byte parses without asserting, and whatever is kept formats. */
/* Whatever damaged text is accepted formats canonically, and that text parses back to the same records. */
static void assert_canonical(const nt_gfx_capture_parsed_t *r) {
    if (r->view.events == NULL) {
        return;
    }
    char *text = format_all(&r->view);
    nt_gfx_event_t again[NT_RECORD_COUNT];
    const nt_gfx_capture_parsed_t back = nt_gfx_capture_parse(text, strlen(text), again, NT_RECORD_COUNT, s_again, sizeof(s_again));
    TEST_ASSERT_NULL(back.error);
    TEST_ASSERT_EQUAL_UINT32(r->view.count, back.view.count);
    TEST_ASSERT_EQUAL_UINT32(r->view.string_bytes, back.view.string_bytes);
    TEST_ASSERT_EQUAL_MEMORY(r->view.strings, back.view.strings, back.view.string_bytes);
    if (back.view.count > 0) { /* Unity refuses an empty compare */
        TEST_ASSERT_EQUAL_MEMORY(r->view.events, again, (size_t)back.view.count * sizeof(*again));
    }
    free(text);
}

static void test_damaged_text_never_asserts(void) {
    const size_t len = strlen(k_expected);
    char *text = (char *)malloc(len + 1U);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_event_t ev[NT_RECORD_COUNT];
    static const char k_bytes[] = {'0', '9', '@', ' ', '"', '\\', 'x', '\n', 'A', '-', '[', ',', '=', '.'};
    for (size_t cut = 0; cut <= len; cut++) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(k_expected, cut, ev, NT_RECORD_COUNT, s_parsed, sizeof(s_parsed));
        assert_canonical(&r);
    }
    for (size_t at = 0; at < len; at++) {
        for (size_t b = 0; b < sizeof(k_bytes); b++) {
            memcpy(text, k_expected, len + 1U);
            text[at] = k_bytes[b];
            const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text, len, ev, NT_RECORD_COUNT, s_parsed, sizeof(s_parsed));
            assert_canonical(&r);
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
    RUN_TEST(test_every_operation_has_a_name);
    RUN_TEST(test_deep_markers_cap_the_indent);
    RUN_TEST(test_a_recorded_frame_prints_one_line_per_record);
    RUN_TEST(test_text_round_trips);
    RUN_TEST(test_a_recorded_frame_round_trips);
    RUN_TEST(test_extremes_round_trip);
    RUN_TEST(test_every_arm_round_trips);
    RUN_TEST(test_every_shape_round_trips);
    RUN_TEST(test_writers_fill_their_fields);
    RUN_TEST(test_documents_parse_back_to_back);
    RUN_TEST(test_bad_text_reports_where_and_keeps_a_prefix);
    RUN_TEST(test_strings_past_the_array_keep_a_prefix);
    RUN_TEST(test_a_saved_web_file_round_trips);
    RUN_TEST(test_damaged_text_never_asserts);
#endif
    return UNITY_END();
}
