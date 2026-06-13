#include "ui/nt_ui_modal.h"

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_MODAL_DEF = {
    .name = "nt_modal",
    .pill_color = 0xFFB070D0U,
    ._reserved = 0U,
};

nt_ui_modal_style_t nt_ui_modal_style_defaults(void) {
    return (nt_ui_modal_style_t){
        .ease_speed = 14.0F,
        .scale_start = 0.92F, /* scale-pop start; eases to 1.0 (Material/iOS default) */
        .backdrop_alpha = 0.55F,
        .slide_offset = 32.0F,
        .backdrop_color = 0xFF000000U, /* black; alpha is scaled by t*backdrop_alpha */
        .layer = 0U,
        .flags = (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP | NT_UI_MODAL_TRANSITION_SCALE_POP),
        ._pad = {0U, 0U},
    };
}

/* Wave-0 scaffold: stack push/pop + overflow assert only. Task 2 fills in the floating
 * decls, anim tween, occluder, and close-source scan. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_modal_result_t nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool open) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_begin: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_modal_begin: id must be non-zero");
    NT_ASSERT(style != NULL && "nt_ui_modal_begin: style must be non-NULL");
    NT_ASSERT(ctx->active_modal_depth < NT_UI_MODAL_MAX_DEPTH && "nt_ui_modal_begin: nesting exceeds NT_UI_MODAL_MAX_DEPTH");
    ctx->active_modal_id[ctx->active_modal_depth] = id;
    ++ctx->active_modal_depth;
    (void)open;
    return (nt_ui_modal_result_t){0};
}

void nt_ui_modal_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_end: ctx must be non-NULL");
    NT_ASSERT(ctx->active_modal_depth > 0U && "nt_ui_modal_end: unbalanced begin/end (stack underflow)");
    --ctx->active_modal_depth;
}

bool nt_ui_modal_active(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_active: ctx must be non-NULL");
    return ctx->active_modal_depth > 0U;
}

#ifdef NT_TEST_ACCESS
uint8_t nt_ui_modal_test_stack_depth(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_test_stack_depth: ctx must be non-NULL");
    return ctx->active_modal_depth;
}
#endif
