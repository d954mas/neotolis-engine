#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "unity.h"

#ifndef NT_TEST_CLAY_DEBUG_VIEW
#error "CMake must specify the expected Clay debug view configuration"
#endif

typedef struct {
    uint32_t total;
    uint32_t duplicates;
    uint32_t capacity_errors;
    bool nonempty_message;
    void *last_user_data;
} error_observer_t;

static error_observer_t s_errors;
static void *s_arena;
static uint32_t s_arena_size;

static void capture_error(Clay_ErrorData error) {
    (void)printf("clay_error=%.*s\n", error.errorText.length, error.errorText.chars);
    s_errors.total++;
    s_errors.duplicates += (error.errorType == CLAY_ERROR_TYPE_DUPLICATE_ID) ? 1U : 0U;
    s_errors.capacity_errors += (error.errorType == CLAY_ERROR_TYPE_ARENA_CAPACITY_EXCEEDED) ? 1U : 0U;
    s_errors.nonempty_message = error.errorText.chars != NULL && error.errorText.length > 0;
    s_errors.last_user_data = error.userData;
}

static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data;
    return (Clay_Dimensions){.width = (float)text.length * 8.0F, .height = (float)config->fontSize};
}

void setUp(void) {
    memset(&s_errors, 0, sizeof s_errors);
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount(1024);
    Clay_SetMaxMeasureTextCacheWordCount(2048);
    s_arena_size = Clay_MinMemorySize();
    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_arena_size);
    s_arena = malloc(s_arena_size);
    TEST_ASSERT_NOT_NULL(s_arena);
    Clay_Context *ctx = Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(s_arena_size, s_arena), (Clay_Dimensions){.width = 1000.0F, .height = 800.0F},
                                        (Clay_ErrorHandler){.errorHandlerFunction = capture_error, .userData = &s_errors});
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL_PTR(ctx, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);
    Clay_SetMeasureTextFunction(measure_text, NULL);
    Clay_SetPointerState((Clay_Vector2){.x = -100.0F, .y = -100.0F}, false);
}

void tearDown(void) {
    if (Clay_GetCurrentContext() != NULL) {
        Clay_SetMeasureTextFunction(NULL, NULL);
    }
    Clay_SetCurrentContext(NULL);
    free(s_arena);
    s_arena = NULL;
}

static Clay_RenderCommandArray build_layout(void) {
    Clay_BeginLayout();
    CLAY({.id = CLAY_ID("test_panel"), .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(50)}}, .backgroundColor = {100, 120, 140, 255}}) {
        CLAY_TEXT(CLAY_STRING("probe"), CLAY_TEXT_CONFIG({.fontSize = 16, .textColor = {255, 255, 255, 255}}));
    }
    return Clay_EndLayout();
}

static bool has_debug_heading(Clay_RenderCommandArray cmds) {
    static const char heading[] = "Clay Debug Tools";
    for (int32_t i = 0; i < cmds.length; i++) {
        const Clay_RenderCommand *cmd = &cmds.internalArray[i];
        if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) {
            continue;
        }
        const Clay_StringSlice text = cmd->renderData.text.stringContents;
        if (text.length == (int32_t)(sizeof heading - 1U) && memcmp(text.chars, heading, sizeof heading - 1U) == 0) {
            return true;
        }
    }
    return false;
}

static void test_exact_arena_supports_repeated_layout(void) {
    for (uint32_t frame = 0U; frame < 3U; frame++) {
        const Clay_RenderCommandArray cmds = build_layout();
        TEST_ASSERT_GREATER_THAN_INT32(0, cmds.length);
        const Clay_ElementData panel = Clay_GetElementData(CLAY_ID("test_panel"));
        TEST_ASSERT_TRUE(panel.found);
        TEST_ASSERT_TRUE(panel.boundingBox.width == 100.0F);
        TEST_ASSERT_TRUE(panel.boundingBox.height == 50.0F);
        TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);
    }
    TEST_ASSERT_EQUAL_UINT32(s_arena_size, Clay_MinMemorySize());
    (void)printf("clay_arena_bytes=%u\n", s_arena_size);
}

static void test_debug_toggle_matches_selected_implementation(void) {
    TEST_ASSERT_FALSE(Clay_IsDebugModeEnabled());
    Clay_SetDebugModeEnabled(false);
    TEST_ASSERT_FALSE(Clay_IsDebugModeEnabled());
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);
    Clay_RenderCommandArray plain = build_layout();
    const int32_t plain_count = plain.length;
    TEST_ASSERT_FALSE(has_debug_heading(plain));
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);

    Clay_SetDebugModeEnabled(true);
#if NT_TEST_CLAY_DEBUG_VIEW
    TEST_ASSERT_TRUE(Clay_IsDebugModeEnabled());
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);
    Clay_RenderCommandArray debug = build_layout();
    TEST_ASSERT_GREATER_THAN_INT32(plain_count, debug.length);
    TEST_ASSERT_TRUE(has_debug_heading(debug));
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);
    Clay_SetDebugModeEnabled(false);
    TEST_ASSERT_FALSE(Clay_IsDebugModeEnabled());
    plain = build_layout();
    TEST_ASSERT_EQUAL_INT32(plain_count, plain.length);
    TEST_ASSERT_FALSE(has_debug_heading(plain));
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.total);
#else
    TEST_ASSERT_FALSE(Clay_IsDebugModeEnabled());
    TEST_ASSERT_EQUAL_UINT32(1U, s_errors.total);
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.capacity_errors);
    TEST_ASSERT_TRUE(s_errors.nonempty_message);
    TEST_ASSERT_EQUAL_PTR(&s_errors, s_errors.last_user_data);
    Clay_SetDebugModeEnabled(false);
    TEST_ASSERT_EQUAL_UINT32(1U, s_errors.total);
    plain = build_layout();
    TEST_ASSERT_EQUAL_INT32(plain_count, plain.length);
    TEST_ASSERT_FALSE(has_debug_heading(plain));
    TEST_ASSERT_EQUAL_UINT32(1U, s_errors.total);
#endif
}

static void test_duplicate_id_still_reaches_error_handler(void) {
    Clay_BeginLayout();
    CLAY({.id = CLAY_ID("duplicate"), .layout = {.sizing = {CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20)}}}) {}
    CLAY({.id = CLAY_ID("duplicate"), .layout = {.sizing = {CLAY_SIZING_FIXED(30), CLAY_SIZING_FIXED(30)}}}){}(void)Clay_EndLayout();
    TEST_ASSERT_EQUAL_UINT32(1U, s_errors.duplicates);
    TEST_ASSERT_EQUAL_UINT32(1U, s_errors.total);
    TEST_ASSERT_EQUAL_UINT32(0U, s_errors.capacity_errors);
    TEST_ASSERT_TRUE(s_errors.nonempty_message);
    TEST_ASSERT_EQUAL_PTR(&s_errors, s_errors.last_user_data);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_exact_arena_supports_repeated_layout);
    RUN_TEST(test_debug_toggle_matches_selected_implementation);
    RUN_TEST(test_duplicate_id_still_reaches_error_handler);
    return UNITY_END();
}
