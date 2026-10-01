#include "ui/nt_ui_shape.h"

#include <math.h>
#include <stddef.h>

#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_SHAPE_DEF = {.name = "nt_shape", .pill_color = 0xFF6B8DBCU};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_shape_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_shape_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx());
    NT_ASSERT(style != NULL);
    NT_ASSERT(style->kind == NT_UI_SHAPE_BOX || style->kind == NT_UI_SHAPE_RADIAL);
    NT_ASSERT(style->paint.gradient >= NT_UI_SHAPE_SOLID && style->paint.gradient <= NT_UI_SHAPE_VERTICAL);
    NT_ASSERT(isfinite(style->paint.border_widths.left) && style->paint.border_widths.left >= 0.0F);
    NT_ASSERT(isfinite(style->paint.border_widths.top) && style->paint.border_widths.top >= 0.0F);
    NT_ASSERT(isfinite(style->paint.border_widths.right) && style->paint.border_widths.right >= 0.0F);
    NT_ASSERT(isfinite(style->paint.border_widths.bottom) && style->paint.border_widths.bottom >= 0.0F);
    NT_ASSERT(nt_material_valid(style->material) && nt_material_get_info(style->material)->program.id != 0U && "nt_ui_shape: material needs a program");
    if (style->kind == NT_UI_SHAPE_BOX) {
        NT_ASSERT(isfinite(style->box.top_left) && style->box.top_left >= 0.0F);
        NT_ASSERT(isfinite(style->box.top_right) && style->box.top_right >= 0.0F);
        NT_ASSERT(isfinite(style->box.bottom_right) && style->box.bottom_right >= 0.0F);
        NT_ASSERT(isfinite(style->box.bottom_left) && style->box.bottom_left >= 0.0F);
    } else {
        NT_ASSERT(isfinite(style->radial.angle_start) && isfinite(style->radial.angle_end));
        NT_ASSERT(isfinite(style->radial.inner_radius_norm) && style->radial.inner_radius_norm >= 0.0F && style->radial.inner_radius_norm < 1.0F);
        NT_ASSERT(style->paint.border_widths.left == 0.0F && style->paint.border_widths.top == 0.0F && style->paint.border_widths.right == 0.0F && style->paint.border_widths.bottom == 0.0F &&
                  (style->shadow.color >> 24U) == 0U);
    }
    if ((style->shadow.color >> 24U) != 0U) {
        NT_ASSERT(isfinite(style->shadow.offset_x) && isfinite(style->shadow.offset_y));
        NT_ASSERT(isfinite(style->shadow.spread) && isfinite(style->shadow.softness) && style->shadow.softness >= 0.0F);
    }
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && decl->image.imageData == NULL && decl->backgroundColor.a == 0.0F && decl->userData == NULL);
    }
    nt_ui_shape_style_t *shape = NT_MEM_SCRATCH_ALLOC(nt_ui_shape_style_t);
    nt_ui_image_payload_t *payload = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(shape != NULL && payload != NULL);
    *shape = *style;
    *payload = (nt_ui_image_payload_t){.flags = NT_UI_IMAGE_ANALYTIC_SHAPE, .shape = shape};
    Clay_ElementDeclaration final = decl != NULL ? *decl : (Clay_ElementDeclaration){0};
    final.image = (Clay_ImageElementConfig){.imageData = payload, .nt_defer_culling = true};
    final.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(final);
    nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_SHAPE_DEF, NULL, true);
}

void nt_ui_shape_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx());
    nt_ui_clay_priv_close_element();
}

void nt_ui_shape(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_shape_style_t *style, const Clay_ElementDeclaration *decl) {
    const Clay_ElementDeclaration grow = {.layout.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}};
    nt_ui_shape_begin(ctx, data, style, decl != NULL ? decl : &grow);
    nt_ui_shape_end(ctx);
}
