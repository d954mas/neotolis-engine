/* Phase 56 ext: exclusive CLAY_IMPLEMENTATION TU. Owns Clay's static globals
 * and private-typed bodies that the verbatim Clay-debug-view port + the
 * inspector internal accessors need. nt_ui.c (engine) and nt_ui_inspector.c
 * (public inspector API) include <clay.h> via the public header path; only
 * THIS file defines CLAY_IMPLEMENTATION. */

/* clang-format off */
#define CLAY_IMPLEMENTATION
#include "clay.h"
/* clang-format on */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_clamp.h"
#include "ui/nt_ui_clay_internal.h"
#include "ui/nt_ui_internal.h"

// #region clay module-level access (thin forwarders)
void nt_ui_clay_priv_set_measure_text_cb(Clay_Dimensions (*cb)(Clay_StringSlice, Clay_TextElementConfig *, void *)) { Clay__MeasureText = cb; }

int32_t nt_ui_clay_priv_default_max_element_count(void) { return Clay__defaultMaxElementCount; }

int32_t nt_ui_clay_priv_default_max_measure_text_word_cache_count(void) { return Clay__defaultMaxMeasureTextWordCacheCount; }
// #endregion

// #region clay_context primitive accessors
int32_t nt_ui_clay_priv_layout_elements_length(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_layout_elements_length: clay must be non-NULL");
    return clay->layoutElements.length;
}

float nt_ui_clay_priv_pointer_x(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_pointer_x: clay must be non-NULL");
    return clay->pointerInfo.position.x;
}

float nt_ui_clay_priv_pointer_y(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_pointer_y: clay must be non-NULL");
    return clay->pointerInfo.position.y;
}

int nt_ui_clay_priv_pointer_pressed(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_pointer_pressed: clay must be non-NULL");
    const Clay_PointerDataInteractionState s = clay->pointerInfo.state;
    return (s == CLAY_POINTER_DATA_PRESSED || s == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) ? 1 : 0;
}
// #endregion

// #region inspector_internal_accessors (CHUNK E)
/* Phase 56 ext rework (TU split): these accessors expose just enough of
 * Clay's private surface for the inspector to read element trees / hashmap
 * items / element configs. They live here because each touches Clay_Context
 * fields and Clay__* statics that are only visible inside CLAY_IMPLEMENTATION. */

uint32_t nt_ui_internal_current_open_element_id(void) {
    nt_ui_context_t *ctx = nt_ui_internal_get_inframe_ctx();
    if (ctx == NULL || ctx->clay == NULL) {
        return 0U;
    }
    /* Mirror Clay__GetOpenLayoutElement (clay.h:1325) without going through
     * its private prototype: top of openLayoutElementStack indexes into
     * layoutElements; ->id is the Clay-assigned id. */
    Clay_Context *cc = ctx->clay;
    if (cc->openLayoutElementStack.length <= 0) {
        return 0U;
    }
    const int32_t idx = Clay__int32_tArray_GetValue(&cc->openLayoutElementStack, cc->openLayoutElementStack.length - 1);
    Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&cc->layoutElements, idx);
    return el->id;
}

uint32_t nt_ui_internal_last_emitted_element_id(void) {
    nt_ui_context_t *ctx = nt_ui_internal_get_inframe_ctx();
    if (ctx == NULL || ctx->clay == NULL) {
        return 0U;
    }
    /* Phase 56 ext: text-leaf widget tagging (nt_ui_label).
     * Clay__OpenTextElement (clay.h:1991) appends a text element to
     * layoutElements and assigns ->id via Clay__HashNumber, but it does NOT
     * push the new element onto openLayoutElementStack -- the stack still
     * points at the PARENT. So current_open_element_id returns the parent's
     * id, not the text leaf's. Calling THIS accessor IMMEDIATELY after
     * CLAY_TEXT reads the freshly-added leaf at layoutElements.length-1.
     * Robust against Clay's maxElementsExceeded early-out (length unchanged
     * = 0 returned, no register). */
    Clay_Context *cc = ctx->clay;
    if (cc->layoutElements.length <= 0) {
        return 0U;
    }
    Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&cc->layoutElements, cc->layoutElements.length - 1);
    return el->id;
}

/* Inspector lives in a separate TU but needs to peek at the Clay
 * layoutElements array. The full Clay_Context type is only visible inside
 * this TU (CLAY_IMPLEMENTATION) -- expose two thin accessors so the
 * inspector stays Clay-agnostic. */
int32_t nt_ui_internal_get_layout_element_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_get_layout_element_count: ctx must be non-NULL");
    if (ctx->clay == NULL) {
        return 0;
    }
    return ctx->clay->layoutElements.length;
}

nt_ui_inspector_element_view_t nt_ui_internal_get_layout_element_view(const nt_ui_context_t *ctx, int32_t index) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_get_layout_element_view: ctx must be non-NULL");
    nt_ui_inspector_element_view_t v = {0};
    if (ctx->clay == NULL || index < 0 || index >= ctx->clay->layoutElements.length) {
        return v;
    }
    Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&ctx->clay->layoutElements, index);
    v.id = el->id;
    /* Position via Clay_GetElementData -- requires Clay's current context set
     * (Clay__GetHashMapItem dereferences it). Inspector is called AFTER
     * nt_ui_end (which clears current context), so set it for the lookup
     * and restore afterward to keep the post-end invariant. */
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    Clay_ElementData ed = Clay_GetElementData((Clay_ElementId){.id = el->id});
    Clay_SetCurrentContext(saved);
    if (ed.found) {
        v.x = ed.boundingBox.x;
        v.y = ed.boundingBox.y;
        v.w = ed.boundingBox.width;
        v.h = ed.boundingBox.height;
    } else {
        v.x = 0.0F;
        v.y = 0.0F;
        v.w = el->dimensions.width;
        v.h = el->dimensions.height;
    }
    return v;
}

/* Map Clay's CLAY__ELEMENT_CONFIG_TYPE_* bitmask (Clay__ElementConfigType) to
 * our exposed 8-bit mask. Order mirrors the Clay__DebugGetElementConfigTypeLabel
 * switch at clay.h:3130 (Shared / Text / Aspect / Image / Floating / Clip /
 * Border / Custom). */
static uint8_t inspector_element_config_mask(Clay_LayoutElement *el) {
    uint8_t mask = 0U;
    for (int32_t i = 0; i < el->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&el->elementConfigs, i);
        switch (cfg->type) {
        case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
            mask |= 1U << 0U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
            mask |= 1U << 1U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_ASPECT:
            mask |= 1U << 2U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_IMAGE:
            mask |= 1U << 3U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_FLOATING:
            mask |= 1U << 4U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_CLIP:
            mask |= 1U << 5U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_BORDER:
            mask |= 1U << 6U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_CUSTOM:
            mask |= 1U << 7U;
            break;
        default:
            break;
        }
    }
    return mask;
}

/* DFS pre-order tree walk -- mirrors Clay__RenderDebugLayoutElementsList
 * (clay.h:3151) but writes flat rows to caller-owned storage instead of
 * emitting CLAY({...}) macros. Depth is tracked via a small explicit stack
 * (cap matches Clay's reusableElementIndexBuffer depth budget). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int32_t nt_ui_internal_collect_tree_rows(const nt_ui_context_t *ctx, nt_ui_inspector_tree_row_t *out, int32_t out_cap) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_collect_tree_rows: ctx must be non-NULL");
    NT_ASSERT(out != NULL && "nt_ui_internal_collect_tree_rows: out must be non-NULL");
    NT_ASSERT(out_cap >= 0 && "nt_ui_internal_collect_tree_rows: out_cap must be >= 0");
    if (ctx->clay == NULL || out_cap == 0) {
        return 0;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);

    int32_t written = 0;
    const int32_t roots = ctx->clay->layoutElementTreeRoots.length;

    /* Explicit DFS stack: (element_index, depth, child_cursor). Cap matches
     * Clay's reusableElementIndexBuffer headroom -- if it overflows we stop
     * walking gracefully (inspector is observability). */
    enum { STACK_CAP = 256 };
    struct {
        int32_t elem_idx;
        uint8_t depth;
        int32_t child_cursor;
    } stack[STACK_CAP];
    int32_t sp = 0;

    for (int32_t r = 0; r < roots && written < out_cap; ++r) {
        Clay__LayoutElementTreeRoot *root = Clay__LayoutElementTreeRootArray_Get(&ctx->clay->layoutElementTreeRoots, r);
        if (sp >= STACK_CAP) {
            break;
        }
        stack[sp].elem_idx = root->layoutElementIndex;
        stack[sp].depth = 0U;
        stack[sp].child_cursor = -1;
        sp++;

        while (sp > 0 && written < out_cap) {
            int32_t top = sp - 1;
            Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&ctx->clay->layoutElements, stack[top].elem_idx);
            if (stack[top].child_cursor < 0) {
                /* First visit -- emit row. */
                nt_ui_inspector_tree_row_t *row = &out[written++];
                memset(row, 0, sizeof *row);
                row->id = el->id;
                row->depth = stack[top].depth;
                row->config_mask = inspector_element_config_mask(el);
                Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(el->id);
                if (item != NULL) {
                    row->bbox_x = item->boundingBox.x;
                    row->bbox_y = item->boundingBox.y;
                    row->bbox_w = item->boundingBox.width;
                    row->bbox_h = item->boundingBox.height;
                    row->offscreen = (uint8_t)Clay__ElementIsOffscreen(&item->boundingBox);
                }
                Clay_String idStr = ctx->clay->layoutElementIdStrings.internalArray[stack[top].elem_idx];
                row->id_string = idStr.chars;
                row->id_string_len = (uint16_t)((idStr.length < 0) ? 0 : idStr.length);
                row->is_text = (uint8_t)Clay__ElementHasConfig(el, CLAY__ELEMENT_CONFIG_TYPE_TEXT);
                if (row->is_text) {
                    Clay__TextElementData *td = el->childrenOrTextContent.textElementData;
                    if (td != NULL) {
                        row->text_chars = td->text.chars;
                        row->text_len = (uint16_t)((td->text.length < 0) ? 0 : td->text.length);
                    }
                }
                stack[top].child_cursor = 0;
                if (row->is_text) {
                    /* TEXT element has no recursable children -- pop now. */
                    sp--;
                    continue;
                }
            }
            /* Push next child or pop. */
            const int32_t childCount = el->childrenOrTextContent.children.length;
            if (stack[top].child_cursor < childCount) {
                int32_t child_idx = el->childrenOrTextContent.children.elements[stack[top].child_cursor];
                stack[top].child_cursor++;
                if (sp >= STACK_CAP) {
                    /* Stack overflow -- stop walking. */
                    sp = 0;
                    break;
                }
                if (stack[top].depth >= UINT8_MAX - 1U) {
                    /* Skip pushing -- depth would overflow. */
                    continue;
                }
                stack[sp].elem_idx = child_idx;
                stack[sp].depth = (uint8_t)(stack[top].depth + 1U);
                stack[sp].child_cursor = -1;
                sp++;
            } else {
                sp--;
            }
        }
    }

    Clay_SetCurrentContext(saved);
    return written;
}

nt_ui_inspector_element_info_t nt_ui_internal_get_element_info(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_get_element_info: ctx must be non-NULL");
    nt_ui_inspector_element_info_t info = {0};
    if (ctx->clay == NULL || id == 0U) {
        return info;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(id);
    if (item == NULL || item->layoutElement == NULL) {
        Clay_SetCurrentContext(saved);
        return info;
    }
    info.found = true;
    info.bbox_x = item->boundingBox.x;
    info.bbox_y = item->boundingBox.y;
    info.bbox_w = item->boundingBox.width;
    info.bbox_h = item->boundingBox.height;
    info.id_string = item->elementId.stringId.chars;
    info.id_string_len = (uint16_t)((item->elementId.stringId.length < 0) ? 0 : item->elementId.stringId.length);
    Clay_LayoutConfig *lc = item->layoutElement->layoutConfig;
    info.layout_direction = (uint8_t)lc->layoutDirection;
    info.padding_l = lc->padding.left;
    info.padding_r = lc->padding.right;
    info.padding_t = lc->padding.top;
    info.padding_b = lc->padding.bottom;
    info.child_gap = lc->childGap;
    info.child_align_x = (uint8_t)lc->childAlignment.x;
    info.child_align_y = (uint8_t)lc->childAlignment.y;
    info.config_mask = inspector_element_config_mask(item->layoutElement);

    for (int32_t i = 0; i < item->layoutElement->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&item->layoutElement->elementConfigs, i);
        if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
            info.bg_r = cfg->config.sharedElementConfig->backgroundColor.r;
            info.bg_g = cfg->config.sharedElementConfig->backgroundColor.g;
            info.bg_b = cfg->config.sharedElementConfig->backgroundColor.b;
            info.bg_a = cfg->config.sharedElementConfig->backgroundColor.a;
            info.corner_tl = cfg->config.sharedElementConfig->cornerRadius.topLeft;
            info.corner_tr = cfg->config.sharedElementConfig->cornerRadius.topRight;
            info.corner_bl = cfg->config.sharedElementConfig->cornerRadius.bottomLeft;
            info.corner_br = cfg->config.sharedElementConfig->cornerRadius.bottomRight;
        } else if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_TEXT) {
            info.text_font_size = cfg->config.textElementConfig->fontSize;
            info.text_font_id = cfg->config.textElementConfig->fontId;
            info.text_color_r = cfg->config.textElementConfig->textColor.r;
            info.text_color_g = cfg->config.textElementConfig->textColor.g;
            info.text_color_b = cfg->config.textElementConfig->textColor.b;
            info.text_color_a = cfg->config.textElementConfig->textColor.a;
            info.text_align = (uint8_t)cfg->config.textElementConfig->textAlignment;
        }
    }
    Clay_SetCurrentContext(saved);
    return info;
}
// #endregion

// #region inspector_emit_layout (verbatim Clay debug view port)
/* Phase 56 ext rework: verbatim port of Clay__RenderDebugView (clay.h:3392-3800)
 * adapted to run inside the user's layout pass (between user CLAY blocks and
 * Clay_EndLayout in nt_ui_end). Lives in this TU because the body touches
 * Clay private types (Clay_Context fields, Clay__GetHashMapItem,
 * Clay__ElementIsOffscreen, layoutElements / layoutElementTreeRoots /
 * reusableElementIndexBuffer / pointerInfo / pointerOverIds / etc.) that are
 * file-static in clay.h.
 *
 * Lines 3113-3122 of clay.h define the palette + metrics (COLOR_1..4,
 * SELECTED_ROW, ROW_HEIGHT=30, OUTER_PADDING=10, INDENT_WIDTH=16). Reproduced
 * verbatim here as static const. */

static const Clay_Color CDV_COLOR_1 = {58, 56, 52, 255};
static const Clay_Color CDV_COLOR_2 = {62, 60, 58, 255};
static const Clay_Color CDV_COLOR_3 = {141, 133, 135, 255};
static const Clay_Color CDV_COLOR_4 = {238, 226, 231, 255};
static const Clay_Color CDV_COLOR_SELECTED_ROW = {102, 80, 78, 255};
static const Clay_Color CDV_HIGHLIGHT_COLOR = {168, 66, 28, 100}; /* Clay__debugViewHighlightColor */
/* Phase 56 ext (REVIEW-2 P3-1): the verbatim Clay debug-view literals
 * (CDV_ROW_HEIGHT 30, CDV_OUTER_PADDING 10, CDV_INDENT_WIDTH 16) and the
 * shared sidebar width NT_UI_INSPECTOR_PANEL_WIDTH moved to runtime config
 * on ctx->inspector_metrics. Each emit function caches local consts of the
 * correct type at entry and uses those throughout. Defaults preserved
 * verbatim in NT_UI_INSPECTOR_METRICS_DEFAULT. */

/* Phase 56 ext fix (viewport-hover propagation): set of Clay element ids the
 * inspector itself owns -- the sidebar panel, its scroll/content wrappers, the
 * close button, and the floating element-highlight rect + its inner
 * rectangle. The viewport-hover propagation in cdv_render_layout_elements_list
 * scans context->pointerOverIds to find the first user element under the
 * cursor; without this filter, a previous frame's floating highlight rect
 * (which attaches at the user element's bbox via parentId) would be picked
 * up, causing a self-feedback loop on the highlight target. The geometric
 * panel-area check filters the sidebar wrappers but NOT the floating
 * highlight (which lives in the VIEWPORT, not the panel), so a small named-id
 * set is the simplest robust filter. Cached lazily on first call -- the
 * Clay__HashString computes are cheap but constant across frames. */
enum { CDV_OWNED_ID_COUNT = 7 };
static uint32_t s_cdv_owned_ids[CDV_OWNED_ID_COUNT];
static bool s_cdv_owned_ids_init = false;

static void cdv_init_owned_ids_once(void) {
    if (s_cdv_owned_ids_init) {
        return;
    }
    s_cdv_owned_ids[0] = Clay__HashString(CLAY_STRING("ntInsp_Root"), 0, 0).id;
    s_cdv_owned_ids[1] = Clay__HashString(CLAY_STRING("ntInsp_OuterScrollPane"), 0, 0).id;
    s_cdv_owned_ids[2] = Clay__HashString(CLAY_STRING("ntInsp_PaneOuter"), 0, 0).id;
    s_cdv_owned_ids[3] = Clay__HashString(CLAY_STRING("ntInsp_CloseButton"), 0, 0).id;
    s_cdv_owned_ids[4] = Clay__HashString(CLAY_STRING("ntInsp_ElementHighlight"), 0, 0).id;
    s_cdv_owned_ids[5] = Clay__HashString(CLAY_STRING("ntInsp_ElementHighlightRectangle"), 0, 0).id;
    /* Clay__RootContainer is the auto-emitted root that covers the entire
     * canvas (clay.h:4186). It would always be in pointerOverIds when any
     * point is inside the viewport -- skipping it ensures the propagation
     * picks an actual user-emitted element. */
    s_cdv_owned_ids[6] = Clay__HashString(CLAY_STRING("Clay__RootContainer"), 0, 0).id;
    s_cdv_owned_ids_init = true;
}

static bool cdv_is_inspector_owned_id(uint32_t id) {
    for (int32_t i = 0; i < CDV_OWNED_ID_COUNT; ++i) {
        if (s_cdv_owned_ids[i] == id) {
            return true;
        }
    }
    return false;
}

/* Mirror of Clay__DebugGetElementConfigTypeLabel (clay.h:3130-3140) but
 * collapsed to just label + color (no inner struct). */
static const char *cdv_config_label(uint8_t type) {
    switch (type) {
    case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
        return "Shared";
    case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
        return "Text";
    case CLAY__ELEMENT_CONFIG_TYPE_ASPECT:
        return "Aspect";
    case CLAY__ELEMENT_CONFIG_TYPE_IMAGE:
        return "Image";
    case CLAY__ELEMENT_CONFIG_TYPE_FLOATING:
        return "Floating";
    case CLAY__ELEMENT_CONFIG_TYPE_CLIP:
        return "Scroll";
    case CLAY__ELEMENT_CONFIG_TYPE_BORDER:
        return "Border";
    case CLAY__ELEMENT_CONFIG_TYPE_CUSTOM:
        return "Custom";
    default:
        return "Error";
    }
}

static Clay_Color cdv_config_color(uint8_t type) {
    switch (type) {
    case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
        return (Clay_Color){243, 134, 48, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
        return (Clay_Color){105, 210, 231, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_ASPECT:
        return (Clay_Color){101, 149, 194, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_IMAGE:
        return (Clay_Color){121, 189, 154, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_FLOATING:
        return (Clay_Color){250, 105, 0, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_CLIP:
        return (Clay_Color){242, 196, 90, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_BORDER:
        return (Clay_Color){108, 91, 123, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_CUSTOM:
        return (Clay_Color){11, 72, 107, 255};
    default:
        return (Clay_Color){0, 0, 0, 255};
    }
}

/* Engine extension column: widget descriptor pill (button/image/label/panel/
 * group OR any game widget). Pulls from ctx->widget_registry via
 * nt_ui_widget_lookup -> def. Plain Clay rows have def==NULL (no pill). */
static inline Clay_Color cdv_widget_color_from_packed(uint32_t packed) {
    /* Packed format matches nt_ui style packing: 0xAABBGGRR. */
    Clay_Color c;
    c.r = (float)(packed & 0xFFU);
    c.g = (float)((packed >> 8) & 0xFFU);
    c.b = (float)((packed >> 16) & 0xFFU);
    c.a = (float)((packed >> 24) & 0xFFU);
    return c;
}

/* Engine extension column: layer number from nt_ui_element_data_t.layer (via
 * Clay's userData). Returns -1 if no userData is found on any config.
 *
 * Reads userData from BOTH SHARED and TEXT configs. Clay auto-routes
 * Clay_ElementDeclaration.userData into a SHARED config (clay.c:2065-2071), so
 * images / panels / customs that pass .userData = data are readable via SHARED.
 * CLAY_TEXT bypasses SHARED: nt_ui_label sets .userData on the TEXT config
 * itself (Clay_TextElementConfig has its own userData slot at clay.h:386), so
 * the TEXT branch is required to recover the layer for label leaves. Note that
 * Clay_ImageElementConfig and Clay_CustomElementConfig do NOT have userData
 * fields (clay.h:424-426, 521-525) -- they piggy-back on SHARED. */
static int32_t cdv_element_layer(const nt_ui_context_t *ctx, Clay_LayoutElement *el) {
    (void)ctx; /* kept in signature for symmetry with other inspector accessors */
    for (int32_t i = 0; i < el->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&el->elementConfigs, i);
        void *u = NULL;
        switch (cfg->type) {
        case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
            u = cfg->config.sharedElementConfig->userData;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
            u = cfg->config.textElementConfig->userData;
            break;
        default:
            break;
        }
        if (u != NULL) {
            return (int32_t)((const nt_ui_element_data_t *)u)->layer;
        }
    }
    return -1;
}

/* Clay__IntToString does int -> Clay_String. We can't see that file-static
 * symbol from this TU (it's also static), so reimplement using static buffers
 * cycled per call. The verbatim Clay uses static buffers internally too.
 *
 * Phase 56 ext bug-fix (layer column reported the LAST element's layer for
 * every row): the previous 16-slot ring buffer wrapped LONG before Clay's
 * layout solve consumed the Clay_String pointers. With ~9 int-to-string calls
 * in the info pane alone + 1 per tree row's "L:N" cell, a 30-row tree calls
 * this ~40 times per frame; slot N got overwritten 2-3 times before render,
 * so every CLAY_TEXT(cdv_int_to_string(layer)) pointer aliased the LAST
 * snprintf result. Bumping the ring to 512 slots covers any practical UI tree
 * + info pane in a single frame; the cursor still wraps but only once the
 * widget count exceeds the cap (the ring is cleared each frame implicitly
 * because all consumers run within the layout pass). 8 KB BSS cost is
 * negligible vs the visual-correctness gain. */
#ifndef NT_UI_INSPECTOR_INT_BUFS
#define NT_UI_INSPECTOR_INT_BUFS 512
#endif
_Static_assert((NT_UI_INSPECTOR_INT_BUFS & (NT_UI_INSPECTOR_INT_BUFS - 1)) == 0, "NT_UI_INSPECTOR_INT_BUFS must be a power of two");
static char cdv_int_bufs[NT_UI_INSPECTOR_INT_BUFS][16];
static uint32_t cdv_int_buf_cursor = 0U;
static Clay_String cdv_int_to_string(int32_t v) {
    char *buf = cdv_int_bufs[cdv_int_buf_cursor];
    cdv_int_buf_cursor = (cdv_int_buf_cursor + 1U) & (NT_UI_INSPECTOR_INT_BUFS - 1U);
    const int n = snprintf(buf, sizeof cdv_int_bufs[0], "%d", v);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Same ring strategy for hex IDs (string-id-empty fallback). 8-char hex +
 * "#" prefix + NUL fits in 16. Separate ring so hex and decimal don't
 * compete for slots. */
static char cdv_hex_bufs[NT_UI_INSPECTOR_INT_BUFS][16];
static uint32_t cdv_hex_buf_cursor = 0U;
static Clay_String cdv_hex_id_to_string(uint32_t v) {
    char *buf = cdv_hex_bufs[cdv_hex_buf_cursor];
    cdv_hex_buf_cursor = (cdv_hex_buf_cursor + 1U) & (NT_UI_INSPECTOR_INT_BUFS - 1U);
    const int n = snprintf(buf, sizeof cdv_hex_bufs[0], "#%08X", v);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Same ring strategy for "#RRGGBBAA" color hex strings (SHARED bg-color
 * inline display per Phase 56 ext UX feedback). Shares the hex ring's
 * cursor space implicitly because each frame resets cdv_hex_buf_cursor to
 * 0 and the ring is sized for the worst-case row count. 10 chars ("#"
 * + 8 hex + NUL) fits in 16. */
static Clay_String cdv_color_hex_to_string(Clay_Color c) {
    char *buf = cdv_hex_bufs[cdv_hex_buf_cursor];
    cdv_hex_buf_cursor = (cdv_hex_buf_cursor + 1U) & (NT_UI_INSPECTOR_INT_BUFS - 1U);
    const uint8_t r = nt_clamp_f_to_u8(c.r);
    const uint8_t g = nt_clamp_f_to_u8(c.g);
    const uint8_t b = nt_clamp_f_to_u8(c.b);
    const uint8_t a = nt_clamp_f_to_u8(c.a);
    const int n = snprintf(buf, sizeof cdv_hex_bufs[0], "#%02X%02X%02X%02X", r, g, b, a);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Phase 56 ext: persistent collapsed-id set helpers. Linear scan -- N <=
 * NT_UI_INSPECTOR_COLLAPSED_CAP (128). At-cap adds are silently dropped so
 * the user can still operate but loses no existing collapses. */
static bool cdv_is_collapsed(const nt_ui_context_t *ctx, uint32_t id) {
    for (uint32_t i = 0; i < ctx->inspector_collapsed_count; ++i) {
        if (ctx->inspector_collapsed_ids[i] == id) {
            return true;
        }
    }
    return false;
}

static void cdv_toggle_collapsed(nt_ui_context_t *ctx, uint32_t id) {
    if (id == 0U) {
        return;
    }
    for (uint32_t i = 0; i < ctx->inspector_collapsed_count; ++i) {
        if (ctx->inspector_collapsed_ids[i] == id) {
            /* Remove via swap-with-last (order-independent set). */
            ctx->inspector_collapsed_ids[i] = ctx->inspector_collapsed_ids[ctx->inspector_collapsed_count - 1U];
            ctx->inspector_collapsed_count--;
            return;
        }
    }
    if (ctx->inspector_collapsed_count < NT_UI_INSPECTOR_COLLAPSED_CAP) {
        ctx->inspector_collapsed_ids[ctx->inspector_collapsed_count++] = id;
    }
    /* At cap: silently drop (no assert; observability not correctness). */
}

/* Forward declaration -- mutual recursion with the tree walk. */
typedef struct {
    int32_t row_count;
    int32_t selected_element_row_index;
} cdv_layout_data_t;

/* Verbatim port of Clay__RenderDebugLayoutElementsList (clay.h:3151-3308).
 * Adapted only for:
 *   - reuses ctx->treeNodeVisited if available; falls back to a private stack
 *     when Clay's `treeNodeVisited` isn't pre-sized.
 *   - hooks ctx->inspector_highlight_id when a row is hovered + ctx->
 *     inspector_selected_id on click (mirror of Clay's debugSelectedElementId).
 *   - emits the engine widget-tag pill + layer column at the tail of each row. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
static cdv_layout_data_t cdv_render_layout_elements_list(nt_ui_context_t *ctx, int32_t initial_roots_length, int32_t highlighted_row_index) {
    Clay_Context *context = ctx->clay;
    /* Phase 56 ext (REVIEW-2 P3-1): runtime metrics cached at entry. Pre-fix
     * these were file-scope #defines (CDV_ROW_HEIGHT 30, CDV_INDENT_WIDTH 16,
     * literal .fontSize 16). Casts match the Clay struct types: Clay_Padding
     * uses uint16_t; CLAY_TEXT_CONFIG .fontSize is uint16_t. */
    const float row_h = ctx->inspector_metrics.row_height;
    const uint16_t font_sz = ctx->inspector_metrics.font_size;
    const uint16_t indent_w = (uint16_t)ctx->inspector_metrics.indent_width;
    /* Private DFS stack -- avoids mutating Clay's reusableElementIndexBuffer
     * which is otherwise touched by Clay's own layout solve. Cap matches
     * Clay's headroom (max element depth = 256). At-cap the walk stops
     * gracefully; the inspector is observability, not correctness. */
    enum { CDV_DFS_CAP = 256 };
    int32_t dfs_elems[CDV_DFS_CAP];
    bool dfs_visited[CDV_DFS_CAP];
    /* Phase 56 ext fix: track whether this frame emitted indent wrappers (the
     * 3 anonymous Clay__OpenElement blocks at the children-recurse branch). If
     * a frame is filtered out (no identity), we did NOT open them and must NOT
     * close them on the second-visit pass either. Mirrors dfs_visited shape. */
    bool dfs_opened_wrappers[CDV_DFS_CAP];
    int32_t dfs_length = 0;
    Clay__DebugView_ScrollViewItemLayoutConfig = (Clay_LayoutConfig){.sizing = {.height = CLAY_SIZING_FIXED(row_h)}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}};
    cdv_layout_data_t layoutData = {0};
    uint32_t highlightedElementId = 0U;
    /* Phase 56 ext: every inspector-owned CLAY/CLAY_TEXT block must carry a
     * layer-PANEL element_data so the walker's layer sort places it strictly
     * above any game UI. Without this, inner blocks fall back to layer 0 and
     * any game UI emitted with a non-zero layer below PANEL (e.g. game HUD
     * at layer 200) could render BETWEEN the inspector root (250) and its
     * inner children.
     *
     * Phase 56 ext perf: split into TWO debug layers so the walker batches
     * the inspector emits cleanly:
     *   - debug_bg_data   (PANEL_BG   = 250) for CLAY containers (rect/border/
     *     image backgrounds + structural wrappers that carry no draw cmd
     *     themselves but stay in the BG layer for consistency).
     *   - debug_text_data (PANEL_TEXT = 251) for CLAY_TEXT configs.
     * The walker iterates layers ascending so BG (250) paints before TEXT
     * (251) -- row backgrounds still appear under their labels. Within each
     * layer the dispatch is monotype, so the sprite pipeline flushes once
     * after all BG cmds and the text pipeline flushes once after all TEXT
     * cmds per zIndex/scissor segment. Pre-split alternation count was
     * O(rows); post-split it is ~2 per segment (one BG->TEXT boundary). */
    void *const debug_bg_data = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_BG);
    void *const debug_text_data = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_TEXT);
    Clay_TextElementConfig debug_text_name_cfg_storage = Clay__DebugView_TextNameConfig;
    debug_text_name_cfg_storage.userData = debug_text_data;
    Clay_TextElementConfig *const debug_text_name_cfg = Clay__StoreTextElementConfig(debug_text_name_cfg_storage);

    for (int32_t rootIndex = 0; rootIndex < initial_roots_length; ++rootIndex) {
        dfs_length = 0;
        Clay__LayoutElementTreeRoot *root = Clay__LayoutElementTreeRootArray_Get(&context->layoutElementTreeRoots, rootIndex);
        if (dfs_length >= CDV_DFS_CAP) {
            break;
        }
        dfs_elems[dfs_length] = root->layoutElementIndex;
        dfs_visited[dfs_length] = false;
        dfs_opened_wrappers[dfs_length] = false;
        dfs_length++;
        if (rootIndex > 0) {
            CLAY(
                {.id = CLAY_IDI("ntInsp_EmptyRowOuter", rootIndex), .layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = {(uint16_t)(indent_w / 2U), 0, 0, 0}}, .userData = debug_bg_data}) {
                CLAY({.id = CLAY_IDI("ntInsp_EmptyRow", rootIndex),
                      .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(row_h)}},
                      .userData = debug_bg_data,
                      .border = {.color = CDV_COLOR_3, .width = {.top = 1}}}) {}
            }
            layoutData.row_count++;
        }
        while (dfs_length > 0) {
            int32_t currentElementIndex = dfs_elems[dfs_length - 1];
            Clay_LayoutElement *currentElement = Clay_LayoutElementArray_Get(&context->layoutElements, (int)currentElementIndex);
            if (dfs_visited[dfs_length - 1]) {
                /* Phase 56 ext fix: only close the 3 indent wrappers when this
                 * frame actually opened them (filtered frames did not). */
                if (dfs_opened_wrappers[dfs_length - 1]) {
                    Clay__CloseElement();
                    Clay__CloseElement();
                    Clay__CloseElement();
                }
                dfs_length--;
                continue;
            }
            dfs_visited[dfs_length - 1] = true;
            Clay_LayoutElementHashMapItem *currentElementData = Clay__GetHashMapItem(currentElement->id);
            bool offscreen = currentElementData != NULL && Clay__ElementIsOffscreen(&currentElementData->boundingBox);
            /* Phase 56 ext extension columns: widget descriptor pill + layer cell. */
            const int32_t layer = cdv_element_layer(ctx, currentElement);
            const nt_ui_widget_def_t *wdef = nt_ui_widget_lookup(ctx, currentElement->id);
            Clay_String idString = context->layoutElementIdStrings.internalArray[currentElementIndex];
            /* Phase 56 ext fix: filter out the 3 anonymous indent wrappers we
             * open per child (lines below) plus any other Clay-auto-anonymous
             * container -- a row is emitted only when the element has a
             * meaningful identity:
             *   has_identity = (stringId.length > 0)
             *               || widget_lookup != NULL
             *               || elementConfigs.length > 0
             *
             * The third clause restores Clay's verbatim debug-view behavior
             * for ANY config-bearing leaf -- Text/Image/SHARED-color/Border/
             * etc. -- which the earlier "kill empty wrappers" pass dropped by
             * accident. Concrete regression: `nt_ui_label` emits a CLAY_TEXT
             * child INSIDE the label's container -- the container has been
             * tagged by the label widget but the Text leaf has no string id
             * and no widget tag, only a Text config. Without this third
             * clause that Text row vanished (user report: "Я теперь не вижу
             * text/image раньше было"). Same applies to standalone CLAY_TEXT
             * blocks, CLAY({ .image = ... }) leaves, and CLAY blocks that
             * only carry a SHARED background color. Truly anonymous wrappers
             * (no config, no id, no widget) still descend silently.
             *
             * The text branch below is exempt -- text content is its own
             * identity. */
            const bool has_identity = (idString.length > 0) || (wdef != NULL) || (currentElement->elementConfigs.length > 0);
            if (has_identity) {
                if (highlighted_row_index == layoutData.row_count) {
                    if (context->pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
                        ctx->inspector_selected_id = currentElement->id;
                    }
                    highlightedElementId = currentElement->id;
                }
                if (ctx->inspector_selected_id == currentElement->id) {
                    layoutData.selected_element_row_index = layoutData.row_count;
                }
            }
            if (has_identity) {
                CLAY({.id = CLAY_IDI("ntInsp_ElementOuter", currentElement->id), .layout = Clay__DebugView_ScrollViewItemLayoutConfig, .userData = debug_bg_data}) {
                    /* Collapse icon / dot. Click on the inner 8x8 toggles the
                     * collapsed-set entry for currentElement->id; collapsed
                     * rows paint the dot in the brighter CDV_COLOR_4 + sharp
                     * (cornerRadius 0) so users can tell at a glance which
                     * subtrees are folded. Verbatim shape from Clay's debug
                     * view -- only the .id + color/radius variation are new. */
                    const bool currently_collapsed = cdv_is_collapsed(ctx, currentElement->id);
                    const Clay_ElementId dotId = Clay__HashString(CLAY_STRING("ntInsp_CollapseDot"), 0, currentElement->id);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                        CLAY({.id = dotId,
                              .layout = {.sizing = {CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8)}},
                              .userData = debug_bg_data,
                              .backgroundColor = currently_collapsed ? CDV_COLOR_4 : CDV_COLOR_3,
                              .cornerRadius = currently_collapsed ? CLAY_CORNER_RADIUS(0) : CLAY_CORNER_RADIUS(2)}) {}
                    }
                    /* Detect click on the dot: same pattern as the close-button
                     * gate at the top of emit_inspector_layout. Pressed THIS
                     * frame + pointer-over-dot id = toggle. */
                    if (context->pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
                        for (int32_t pi = 0; pi < context->pointerOverIds.length; ++pi) {
                            const Clay_ElementId *over = Clay_ElementIdArray_Get(&context->pointerOverIds, pi);
                            if (over->id == dotId.id) {
                                cdv_toggle_collapsed(ctx, currentElement->id);
                                break;
                            }
                        }
                    }
                    if (offscreen) {
                        CLAY({.layout = {.padding = {8, 8, 2, 2}}, .userData = debug_bg_data, .border = {.color = CDV_COLOR_3, .width = {1, 1, 1, 1, 0}}}) {
                            CLAY_TEXT(CLAY_STRING("Offscreen"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}));
                        }
                    }
                    /* Phase 56 ext fix: when stringId is empty (CLAY_IDI / no-id /
                     * Clay-auto-anonymous), Clay's debug view rendered nothing,
                     * leaving the row visually blank. Fall back to the element id
                     * as hex so unnamed elements are still identifiable in the
                     * tree. Buffered via cdv_hex_id_to_string for stable Clay_String
                     * pointers (same ring-buffer fix as the layer column). */
                    if (idString.length > 0) {
                        CLAY_TEXT(idString, offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}) : debug_text_name_cfg);
                    } else {
                        CLAY_TEXT(cdv_hex_id_to_string(currentElement->id),
                                  offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}) : debug_text_name_cfg);
                    }
                    /* Phase 56 ext (CHUNK C, UX fix #5): inline SHARED bg-color
                     * swatch + hex string in the row's identification area --
                     * replaces the standalone "Color" pill that the user found
                     * cryptic ("это мой цвет или нет?"). Surfaced right after
                     * the id text so the color reads as PART of the row's
                     * identity. Only emitted when a SHARED config with non-zero
                     * alpha is attached (matches the pre-fix gate). The matching
                     * pre-fix "Color" pill in the config-loop below is removed
                     * (no equivalent left -- the inline form IS the surface). */
                    for (int32_t cfgScan = 0; cfgScan < currentElement->elementConfigs.length; ++cfgScan) {
                        Clay_ElementConfig *sc = Clay__ElementConfigArraySlice_Get(&currentElement->elementConfigs, cfgScan);
                        if (sc->type != CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            continue;
                        }
                        const Clay_Color bg = sc->config.sharedElementConfig->backgroundColor;
                        if (bg.a <= 0) {
                            break;
                        }
                        /* Swatch quad (16x16, corner-radius 4) + hex text. Sits
                         * in a row container so it shares the vertical center
                         * with the id text. Border color matches the row's
                         * subtle frame style. fontSize 14 keeps the row's
                         * vertical extent unchanged from the verbatim shape. */
                        CLAY({.layout = {.sizing = {.height = CLAY_SIZING_FIXED(row_h - 8.0F)}, .childGap = 4, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)}},
                                  .backgroundColor = bg,
                                  .userData = debug_bg_data,
                                  .cornerRadius = CLAY_CORNER_RADIUS(4),
                                  .border = {.color = CDV_COLOR_4, .width = {1, 1, 1, 1, 0}}}) {}
                            CLAY_TEXT(cdv_color_hex_to_string(bg), CLAY_TEXT_CONFIG({.textColor = offscreen ? CDV_COLOR_3 : CDV_COLOR_4, .fontSize = 14, .userData = debug_text_data}));
                        }
                        break;
                    }
                    /* Phase 56 ext perf (STRATEGY A, reduces ~209 draw calls on
                     * open inspector to <=~80): pill BACKGROUNDS dropped. Pre-fix
                     * each row emitted 3-5 (rect-bg + colored text) pairs:
                     * radius / per-config / widget / layer. The walker flushes the
                     * sprite pipeline on every RECT->TEXT boundary
                     * (engine/ui/nt_ui.c::prep_sprite_dispatch + TEXT branch's
                     * sprite_renderer_flush), so each pair cost ~2 draw calls.
                     * The pill visual identity is preserved by rendering the
                     * label TEXT in the pill's color (orange-ish for SHARED-radius,
                     * per-config color for FLOATING/CLIP/etc., the widget def's
                     * pill_color for the engine widget tag, CDV_COLOR_3 for the
                     * layer column). The color swatch RECT in the inline display
                     * is kept -- it is the only direct visual sample of the
                     * widget's background color and dropping it would defeat the
                     * point. Result: per-row alternations drop from ~6 to ~2
                     * (row_bg + swatch + a long T...T run). Pin:
                     * test_inspector_alternations_capped_after_strategy_a. */
                    for (int32_t elementConfigIndex = 0; elementConfigIndex < currentElement->elementConfigs.length; ++elementConfigIndex) {
                        Clay_ElementConfig *elementConfig = Clay__ElementConfigArraySlice_Get(&currentElement->elementConfigs, elementConfigIndex);
                        if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            /* Radius marker -- orange-ish text (same hue as the pre-fix bg pill),
                             * no rect background. The label fontSize stays at 16 so the row
                             * vertical extent matches the rest. */
                            Clay_CornerRadius radius = elementConfig->config.sharedElementConfig->cornerRadius;
                            if (radius.bottomLeft > 0) {
                                CLAY_TEXT(CLAY_STRING("Radius"), CLAY_TEXT_CONFIG({.textColor = {243, 134, 48, 255}, .fontSize = font_sz, .userData = debug_text_data}));
                            }
                            continue;
                        }
                        Clay_Color config_color = cdv_config_color((uint8_t)elementConfig->type);
                        const char *labelStr = cdv_config_label((uint8_t)elementConfig->type);
                        /* Config-type label in the config's hue, no rect background. */
                        CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(labelStr), .chars = labelStr}),
                                  CLAY_TEXT_CONFIG({.textColor = config_color, .fontSize = font_sz, .userData = debug_text_data}));
                    }
                    /* Engine widget descriptor name in the descriptor's pill_color hue, no rect. */
                    if (wdef != NULL && wdef->name != NULL) {
                        Clay_Color wbg = cdv_widget_color_from_packed(wdef->pill_color);
                        CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(wdef->name), .chars = wdef->name}), CLAY_TEXT_CONFIG({.textColor = wbg, .fontSize = font_sz, .userData = debug_text_data}));
                    }
                    /* Layer cell -- "L:N". No rect background or border; the L:N
                     * label reads as a column tag on its own. */
                    if (layer >= 0) {
                        CLAY_TEXT(CLAY_STRING("L:"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}));
                        CLAY_TEXT(cdv_int_to_string(layer), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = font_sz, .userData = debug_text_data}));
                    }
                }
            } /* if (has_identity) */

            /* Text-content row (verbatim from clay.h:3258-3270, with engine
             * addition: hit-test the text-content row too).
             *
             * Phase 56 ext fix: Clay's verbatim port treats this row as
             * non-interactive -- the hit-test at line ~939 only fires on the
             * element row (row N), so a click on the text-content row (row
             * N+1) was a silent no-op leaving inspector_selected_id unchanged
             * (user-visible symptom: "clicking text selects the last selected
             * one"). After commit ab6d235 (nt_ui_label registers its widget
             * tag on the CLAY_TEXT leaf), the text-content row IS describing
             * a meaningful, identified widget -- the user reasonably expects
             * clicking it to select that widget. The text-content row maps to
             * the SAME currentElement->id as the element row above it (Clay
             * does not split text leaves into two distinct elements), so we
             * mirror the line ~939 hit-test here against row N+1. */
            if (Clay__ElementHasConfig(currentElement, CLAY__ELEMENT_CONFIG_TYPE_TEXT)) {
                layoutData.row_count++;
                if (highlighted_row_index == layoutData.row_count) {
                    if (context->pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
                        ctx->inspector_selected_id = currentElement->id;
                    }
                    highlightedElementId = currentElement->id;
                }
                Clay__TextElementData *textElementData = currentElement->childrenOrTextContent.textElementData;
                Clay_TextElementConfig *rawTextConfig = offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}) : debug_text_name_cfg;
                CLAY({.layout = {.sizing = {.height = CLAY_SIZING_FIXED(row_h)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                    CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED((float)(indent_w + 16U))}}, .userData = debug_bg_data}) {}
                    CLAY_TEXT(CLAY_STRING("\""), rawTextConfig);
                    if (textElementData != NULL) {
                        CLAY_TEXT(textElementData->text.length > 40 ? ((Clay_String){.length = 40, .chars = textElementData->text.chars}) : textElementData->text, rawTextConfig);
                        if (textElementData->text.length > 40) {
                            CLAY_TEXT(CLAY_STRING("..."), rawTextConfig);
                        }
                    }
                    CLAY_TEXT(CLAY_STRING("\""), rawTextConfig);
                }
            } else if (has_identity && currentElement->childrenOrTextContent.children.length > 0 && !cdv_is_collapsed(ctx, currentElement->id)) {
                /* Only open the 3 indent wrappers when a row was emitted AND
                 * the subtree is not collapsed -- filtered (no-identity) and
                 * collapsed elements descend SILENTLY so the indent steps
                 * (vertical line + padding) do not appear for hidden subtrees.
                 * Phase 56 ext: each wrapper carries the PANEL layer so the
                 * walker keeps them grouped above any game UI. */
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.padding = {.left = 8}}, .userData = debug_bg_data});
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.padding = {.left = indent_w}}, .userData = debug_bg_data, .border = {.color = CDV_COLOR_3, .width = {.left = 1}}});
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data});
                dfs_opened_wrappers[dfs_length - 1] = true;
            }

            /* row_count tracks VISIBLE rows -- filtered frames do not advance it
             * so highlighted_row_index from the pointer-Y math keeps mapping to
             * the user-visible rows correctly. */
            if (has_identity) {
                layoutData.row_count++;
            }
            /* Children are skipped entirely when this id is collapsed --
             * mirrors Clay's debug-view collapsed flag (clay.h:3281). The
             * dot icon click toggles the collapsed-set entry for this id. */
            if (!Clay__ElementHasConfig(currentElement, CLAY__ELEMENT_CONFIG_TYPE_TEXT) && !cdv_is_collapsed(ctx, currentElement->id)) {
                const int32_t childLen = currentElement->childrenOrTextContent.children.length;
                int32_t *childElems = currentElement->childrenOrTextContent.children.elements;
                /* Clay's lifecycle: children.elements is assigned in Clay__CloseElement (clay.h:1828).
                 * Our verbatim port is invoked from nt_ui_end BEFORE Clay_EndLayout, so the
                 * auto-emitted Clay__RootContainer (always element 0) is still OPEN -- its
                 * children.elements is NULL even though children.length tracks how many user
                 * top-level CLAY blocks closed under it. For that single case the live child
                 * indices are at the BOTTOM of context->layoutElementChildrenBuffer (the buffer
                 * Clay uses for in-flight children; clay.h:1906). Every other element on the
                 * walk path either has children.elements already populated (closed normally)
                 * or is genuinely a leaf. */
                if (childLen > 0 && childElems == NULL && currentElementIndex == 0) {
                    childElems = context->layoutElementChildrenBuffer.internalArray;
                }
                if (childLen > 0 && childElems != NULL) {
                    for (int32_t i = childLen - 1; i >= 0; --i) {
                        if (dfs_length >= CDV_DFS_CAP) {
                            break;
                        }
                        dfs_elems[dfs_length] = childElems[i];
                        dfs_visited[dfs_length] = false;
                        dfs_opened_wrappers[dfs_length] = false;
                        dfs_length++;
                    }
                }
            }
        }
    }

    /* Phase 56 ext fix (viewport-hover propagation): if the sidebar did not
     * pick a row this frame AND the pointer is NOT over the inspector panel,
     * scan context->pointerOverIds for the first user element underneath the
     * cursor and adopt it as the highlight target. Mirrors Clay's stock
     * "hovering a sidebar row highlights the widget", extended to "hovering
     * the widget itself in the viewport also highlights it in the sidebar".
     * pointerOverIds is populated by Clay_SetPointerState (called in
     * nt_ui_begin) and contains every element under the pointer in z-order.
     * cdv_is_inspector_owned_id filters out the inspector's own named
     * elements (panel root, scroll wrappers, close button, floating
     * highlight rect -- the last one is critical: it lives in the viewport
     * attached to the user element and would otherwise self-feedback). The
     * geometric panel-area check filters anything inside the sidebar
     * footprint (per-row wrappers, indent containers, etc.). The first id
     * that passes BOTH filters wins. */
    if (highlightedElementId == 0U && !ctx->inspector_pointer_consumed) {
        const float panel_left_x = context->layoutDimensions.width - ctx->inspector_metrics.panel_width;

        /* Phase 56 ext fix (transform-aware hover): scan debug_zones FIRST.
         * Recorded zones carry the declaration-time accum stack snapshot, so
         * we can inverse-affine the pointer through each zone's own transform
         * and point-in-rect against the (untransformed) visual bbox -- same
         * math as ui_hit_test that the click/capture path uses. This closes
         * the divergence the user reported: Clay's pointerOverIds is axis-
         * aligned in LAYOUT space; for transformed widgets (e.g. the demo's
         * BAKED button rotated 25 deg, or the runtime-rotated row via Q/E)
         * the hover misses the visually-correct widget while the click still
         * lands on it -- a confusing mismatch.
         *
         * Order: LAST hit wins (deepest in declaration order) -- mirrors the
         * "deepest under pointer" semantics Clay's pointerOverIds end-first
         * scan already follows. The inspector-owned-id filter does not apply
         * here because debug_zones is recorded ONLY by user-widget queries
         * (the inspector itself never calls get_interaction). The panel-area
         * filter still applies, but it compares the SCREEN-SPACE forward-
         * transformed visual center, not the raw layout bbox -- otherwise a
         * widget whose layout sits inside the panel area but renders OUTSIDE
         * it (e.g. a translated grid) would be incorrectly filtered out. */
        const float px = context->pointerInfo.position.x;
        const float py = context->pointerInfo.position.y;
        for (int32_t zi = (int32_t)ctx->debug_zone_count - 1; zi >= 0; --zi) {
            const nt_ui_debug_zone_t *z = &ctx->debug_zones[zi];
            if (z->id == 0U) {
                continue;
            }
            /* Forward-transform the visual center to screen space to test the
             * panel-area filter. compose_transform_level accumulates the
             * affine; apply to (center_x, center_y) which is invariant. */
            float a = 1.0F;
            float b = 0.0F;
            float c = 0.0F;
            float dd = 1.0F;
            float tx = 0.0F;
            float ty = 0.0F;
            for (uint32_t k = 0; k < z->accum_depth; ++k) {
                nt_ui_internal_compose_transform_level(&z->accum[k], z->center_x, z->center_y, &a, &b, &c, &dd, &tx, &ty);
            }
            const float screen_cx = (z->center_x * a) + (z->center_y * b) + tx;
            if (screen_cx >= panel_left_x) {
                continue; /* rendered inside the panel footprint */
            }
            /* Inverse-affine the pointer into the zone's untransformed layout
             * frame, then point-in-rect against the visual bbox. */
            const float det = (a * dd) - (b * c);
            if (det == 0.0F) {
                continue; /* degenerate transform */
            }
            const float inv_a = dd / det;
            const float inv_b = -b / det;
            const float inv_c = -c / det;
            const float inv_d = a / det;
            const float rx = px - tx;
            const float ry = py - ty;
            const float lx = (inv_a * rx) + (inv_b * ry);
            const float ly = (inv_c * rx) + (inv_d * ry);
            if (lx >= z->visual_l && lx <= z->visual_r && ly >= z->visual_t && ly <= z->visual_b) {
                highlightedElementId = z->id;
                break; /* LAST in declaration order = deepest visually */
            }
        }

        /* Clay populates pointerOverIds in DFS pre-order (outermost first,
         * deepest last; see Clay_SetPointerState at clay.h:3935-3973). Scan
         * from END to START so the DEEPEST user element under the pointer
         * wins -- mirrors the visual expectation "hovering the button
         * highlights the button, not the panel that contains it".
         *
         * Two-pass: first prefer a REGISTERED WIDGET id (button/panel/image/
         * label/group OR any game widget). Plain Clay containers nested inside
         * a widget (e.g. nt_ui_button's anonymous BTN_CONTENT child wrapper)
         * are visually part of the widget; surfacing the child id would yield
         * no recorded debug zone AND no hit-padding metadata, so the post-walk
         * overlay's hit-zone visualization (padded touch-target fill +
         * outline) vanishes -- which is the user-visible regression that
         * shipped with the propagation block in 5c600b4. If no registered
         * widget is under the pointer (raw Clay tree, no nt_ui_* widget
         * along the hover path), fall through to the deepest non-owned
         * element so plain rows still highlight.
         *
         * This pointerOverIds fallback runs ONLY when the debug_zones scan
         * above failed -- it covers the case of plain Clay containers (no
         * nt_ui_step_interaction call) that the user can still hover. For
         * transformed widgets, the debug_zones scan above wins because the
         * widget steps interaction inside its begin() (recording the
         * zone with the live accum snapshot) while the pointerOverIds path
         * here is axis-aligned and would miss them.
         *
         * REVIEW-2 ANALYSIS (kept 3-stage): an earlier proposal suggested
         * dropping this widget-preference pass on the assumption that all
         * registered widgets already record a debug_zone via Stage 1, making
         * the preference redundant. That assumption is FALSE: only
         * nt_ui_button calls nt_ui_step_interaction_padded today; panel,
         * group, image, and label widgets register their descriptor but
         * NEVER query interaction (they are non-interactive). For them no
         * debug_zone exists, so Stage 1 cannot pick them, and removing this
         * widget-preference pass would surface the deeper anonymous child
         * instead of the widget itself (regression covered by
         * test_inspector_viewport_hover_prefers_widget_over_child:1450).
         * The widget-vs-fallback split is a real correctness gate, not a
         * legacy optimization. */
        if (highlightedElementId == 0U) {
            uint32_t fallback_id = 0U;
            for (int32_t i = context->pointerOverIds.length - 1; i >= 0; --i) {
                const Clay_ElementId *eid = Clay_ElementIdArray_Get(&context->pointerOverIds, i);
                if (cdv_is_inspector_owned_id(eid->id)) {
                    continue;
                }
                const Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(eid->id);
                if (item == NULL) {
                    continue;
                }
                /* Skip elements whose bbox.x is in the panel footprint -- catches
                 * the indent wrappers, row backgrounds, per-element ElementOuter
                 * blocks, etc. that the cached named-id set does not enumerate. */
                if (item->boundingBox.x >= panel_left_x) {
                    continue;
                }
                if (nt_ui_widget_lookup(ctx, eid->id) != NULL) {
                    highlightedElementId = eid->id;
                    break;
                }
                /* First viable non-widget candidate becomes the fallback. */
                if (fallback_id == 0U) {
                    fallback_id = eid->id;
                }
            }
            if (highlightedElementId == 0U && fallback_id != 0U) {
                highlightedElementId = fallback_id;
            }
        }
    }

    if (highlightedElementId) {
        /* Mirror clay.h:3303 -- floating highlight rectangle attached to the
         * hovered element. This is the IN-VIEWPORT highlight as the user moves
         * the pointer over the sidebar. Tagged with NT_UI_LAYER_DEBUG_HIGHLIGHT
         * (240) so the walker sorts it above any game UI element (typical
         * 0..~10) but BELOW the sidebar panel (250) where they overlap.
         *
         * zIndex is 32764 (was 32767 = above panel) so Clay's zIndex segmentation
         * also keeps the highlight strictly under the panel root (32765). Without
         * both adjustments, highlights for widgets near the screen's right edge
         * leaked through the panel (visible bug in the user's screenshot).
         * Order matters: zIndex segments come first, then per-segment layer sort. */
        CLAY({.id = CLAY_ID("ntInsp_ElementHighlight"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
              .userData = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_HIGHLIGHT),
              .floating = {.parentId = highlightedElementId, .zIndex = 32764, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH, .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID}}) {
            CLAY({.id = CLAY_ID("ntInsp_ElementHighlightRectangle"),
                  .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                  .userData = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_HIGHLIGHT),
                  .backgroundColor = CDV_HIGHLIGHT_COLOR}) {}
        }
        /* Surface the hovered id to the post-walk overlay. */
        ctx->inspector_highlight_id = highlightedElementId;
    } else if (ctx->inspector_selected_id != 0U) {
        /* No hover -- fall back to persistent selection so the overlay still
         * focuses the last clicked sidebar row. */
        ctx->inspector_highlight_id = ctx->inspector_selected_id;
    }
    return layoutData;
}

/* Verbatim port of Clay__RenderDebugView (clay.h:3392-3800). Adapted:
 *   - close button: we still emit it (visual parity) but the click sets
 *     ctx->inspector_active = false on press inside its bounds, not Clay's
 *     debugModeEnabled (which is no longer wired).
 *   - pointer-in-debug-view check uses our panel width (ctx->inspector_metrics.panel_width)
 *     and the 300 px info-pane reservation, same as Clay's literal constants.
 *   - the info pane is a CONDENSED but faithful version of clay.h:3477-3800
 *     (Bounding Box, Layout Direction, Sizing, Padding, Child Gap, Child
 *     Alignment) + a single config-type header per element config + body for
 *     SHARED/TEXT/IMAGE/CLIP/BORDER (Floating/Custom/Aspect ports are
 *     headers-only to keep this TU finite -- the user explicitly accepted
 *     literal-where-possible; truly verbose configs degrade to header only).
 *   - warnings pane is dropped (engine doesn't read Clay warnings here). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_ui_internal_emit_inspector_layout(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(ctx->in_frame);
    NT_ASSERT(ctx->clay != NULL);

    /* Reset the int/hex string rings at the start of each emit so the cursor
     * always begins at 0 -- gives us a deterministic 512-slot window per
     * frame regardless of how many frames have run before. The Clay_String
     * pointers remain stable through the layout solve since no other code
     * writes into these buffers between emit_layout and Clay_EndLayout. */
    cdv_int_buf_cursor = 0U;
    cdv_hex_buf_cursor = 0U;

    /* Phase 56 ext (REVIEW-2 P3-1): runtime metrics cached at entry. Pre-fix
     * these were file-scope #defines + literals. row_h/font_sz are floats /
     * uint16 to match Clay struct types; outer_pad/indent_w are uint16 for
     * Clay_Padding. Defaults: 400 / 30 / 16 / 10 / 16 (verbatim Clay shape). */
    const float panel_w = ctx->inspector_metrics.panel_width;
    const float row_h = ctx->inspector_metrics.row_height;
    const uint16_t font_sz = ctx->inspector_metrics.font_size;
    const uint16_t outer_pad = (uint16_t)ctx->inspector_metrics.outer_padding;

    Clay_Context *context = ctx->clay;
    /* Lazily cache the set of inspector-owned named ids -- used by the
     * viewport-hover propagation inside cdv_render_layout_elements_list to
     * skip our own elements when scanning pointerOverIds. */
    cdv_init_owned_ids_once();
    Clay_ElementId closeButtonId = Clay__HashString(CLAY_STRING("ntInsp_CloseButton"), 0, 0);
    if (context->pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        for (int32_t i = 0; i < context->pointerOverIds.length; ++i) {
            Clay_ElementId *elementId = Clay_ElementIdArray_Get(&context->pointerOverIds, i);
            if (elementId->id == closeButtonId.id) {
                ctx->inspector_active = false;
                return;
            }
        }
    }

    uint32_t initialRootsLength = (uint32_t)context->layoutElementTreeRoots.length;
    uint32_t initialElementsLength = (uint32_t)context->layoutElements.length;
    /* Phase 56 ext: PANEL element_data for every inspector-owned CLAY/CLAY_TEXT
     * block. cdv_render_layout_elements_list constructs its own local copy of
     * the same metadata so its CLAY emits route through the same layers.
     *
     * Phase 56 ext perf: split into BG (250) for CLAY containers and TEXT (251)
     * for CLAY_TEXT configs so the walker batches all inspector rects then all
     * inspector texts per zIndex/scissor segment. See cdv_render_layout_elements_list
     * for the rationale + walker invariant (ascending layer iteration via
     * active_layers bitmask + __builtin_ctz). */
    void *const debug_bg_data = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_BG);
    void *const debug_text_data = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_TEXT);
    Clay_TextElementConfig *infoTextConfig = CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = font_sz, .wrapMode = CLAY_TEXT_WRAP_NONE, .userData = debug_text_data});
    Clay_TextElementConfig *infoTitleConfig = CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .wrapMode = CLAY_TEXT_WRAP_NONE, .userData = debug_text_data});
    Clay_ElementId scrollId = Clay__HashString(CLAY_STRING("ntInsp_OuterScrollPane"), 0, 0);
    float scrollYOffset = 0;
    bool pointerInDebugView = context->pointerInfo.position.y < context->layoutDimensions.height - 300;
    for (int32_t i = 0; i < context->scrollContainerDatas.length; ++i) {
        Clay__ScrollContainerDataInternal *scrollContainerData = Clay__ScrollContainerDataInternalArray_Get(&context->scrollContainerDatas, i);
        if (scrollContainerData->elementId == scrollId.id) {
            if (!context->externalScrollHandlingEnabled) {
                scrollYOffset = scrollContainerData->scrollPosition.y;
            } else {
                pointerInDebugView = context->pointerInfo.position.y + scrollContainerData->scrollPosition.y < context->layoutDimensions.height - 300;
            }
            break;
        }
    }
    int32_t highlightedRow = pointerInDebugView ? (int32_t)((context->pointerInfo.position.y - scrollYOffset) / row_h) - 1 : -1;
    if (context->pointerInfo.position.x < context->layoutDimensions.width - panel_w) {
        highlightedRow = -1;
    }
    cdv_layout_data_t layoutData = {0};
    /* RIGHT_CENTER/RIGHT_CENTER (overlay) instead of Clay's LEFT_CENTER/RIGHT_CENTER (side-by-side):
     * engine disables Clay debug mode so the root is full-width and the verbatim attach
     * would land at [screen.w, screen.w + panel_w] -- entirely off-screen. */
    CLAY({.id = CLAY_ID("ntInsp_Root"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(panel_w), CLAY_SIZING_FIXED(context->layoutDimensions.height)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .userData = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL),
          .floating = {.zIndex = 32765,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_CENTER, .parent = CLAY_ATTACH_POINT_RIGHT_CENTER},
                       .attachTo = CLAY_ATTACH_TO_ROOT,
                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT},
          .border = {.color = CDV_COLOR_3, .width = {.bottom = 1}}}) {
        /* Header bar. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)}, .padding = {outer_pad, outer_pad, 0, 0}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = CDV_COLOR_2,
              .userData = debug_bg_data}) {
            CLAY_TEXT(CLAY_STRING("nt_ui_inspector (Clay debug view port)"), infoTextConfig);
            CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}}, .userData = debug_bg_data}) {}
            /* Close button (verbatim shape from clay.h:3439-3447). */
            CLAY({.id = closeButtonId,
                  .layout = {.sizing = {CLAY_SIZING_FIXED(row_h - 10.0F), CLAY_SIZING_FIXED(row_h - 10.0F)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = {217, 91, 67, 80},
                  .userData = debug_bg_data,
                  .cornerRadius = CLAY_CORNER_RADIUS(4),
                  .border = {.color = {217, 91, 67, 255}, .width = {1, 1, 1, 1, 0}}}) {
                CLAY_TEXT(CLAY_STRING("x"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = font_sz, .userData = debug_text_data}));
            }
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}, .backgroundColor = CDV_COLOR_3, .userData = debug_bg_data}) {}
        CLAY({.id = scrollId,
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
              .userData = debug_bg_data,
              .clip = {.horizontal = true, .vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
                  .backgroundColor = ((initialElementsLength + initialRootsLength) & 1) == 0 ? CDV_COLOR_2 : CDV_COLOR_1,
                  .userData = debug_bg_data}) {
                Clay_ElementId panelContentsId = Clay__HashString(CLAY_STRING("ntInsp_PaneOuter"), 0, 0);
                CLAY({.id = panelContentsId,
                      .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                      .userData = debug_bg_data,
                      .floating = {.zIndex = 32766, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH, .attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT}}) {
                    CLAY(
                        {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = {outer_pad, outer_pad, 0, 0}, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data}) {
                        layoutData = cdv_render_layout_elements_list(ctx, (int32_t)initialRootsLength, highlightedRow);
                    }
                }
                Clay_LayoutElementHashMapItem *panelContentsItem = Clay__GetHashMapItem(panelContentsId.id);
                float contentWidth = panelContentsItem != NULL ? panelContentsItem->layoutElement->dimensions.width : 0.0F;
                CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(contentWidth)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data}) {}
                for (int32_t i = 0; i < layoutData.row_count; i++) {
                    Clay_Color rowColor = (i & 1) == 0 ? CDV_COLOR_2 : CDV_COLOR_1;
                    if (i == layoutData.selected_element_row_index) {
                        rowColor = CDV_COLOR_SELECTED_ROW;
                    }
                    if (i == highlightedRow) {
                        rowColor.r *= 1.25F;
                        rowColor.g *= 1.25F;
                        rowColor.b *= 1.25F;
                    }
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .backgroundColor = rowColor, .userData = debug_bg_data}) {}
                }
            }
        }
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1)}}, .backgroundColor = CDV_COLOR_3, .userData = debug_bg_data}) {}
        if (ctx->inspector_selected_id != 0U) {
            Clay_LayoutElementHashMapItem *selectedItem = Clay__GetHashMapItem(ctx->inspector_selected_id);
            if (selectedItem != NULL) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(300)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
                      .backgroundColor = CDV_COLOR_2,
                      .userData = debug_bg_data,
                      .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
                      .border = {.color = CDV_COLOR_3, .width = {.betweenChildren = 1}}}) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h + 8.0F)}, .padding = {outer_pad, outer_pad, 0, 0}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                          .userData = debug_bg_data}) {
                        CLAY_TEXT(CLAY_STRING("Layout Config"), infoTextConfig);
                        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}}, .userData = debug_bg_data}) {}
                        if (selectedItem->elementId.stringId.length != 0) {
                            CLAY_TEXT(selectedItem->elementId.stringId, infoTitleConfig);
                        }
                    }
                    Clay_Padding attributeConfigPadding = {outer_pad, outer_pad, 8, 8};
                    CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data}) {
                        /* Engine extension rows -- Widget type + Layer.
                         * Surfaced at the TOP of the info pane so the user
                         * sees them first when inspecting an element. */
                        const nt_ui_widget_def_t *sel_def = nt_ui_widget_lookup(ctx, ctx->inspector_selected_id);
                        const char *sel_w_tag = (sel_def != NULL && sel_def->name != NULL) ? sel_def->name : "-";
                        CLAY_TEXT(CLAY_STRING("Widget"), infoTitleConfig);
                        CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(sel_w_tag), .chars = sel_w_tag}), infoTextConfig);
                        CLAY_TEXT(CLAY_STRING("Layer"), infoTitleConfig);
                        const int32_t selLayer = cdv_element_layer(ctx, selectedItem->layoutElement);
                        if (selLayer >= 0) {
                            CLAY_TEXT(cdv_int_to_string(selLayer), infoTextConfig);
                        } else {
                            CLAY_TEXT(CLAY_STRING("(none)"), infoTextConfig);
                        }
                        CLAY_TEXT(CLAY_STRING("Bounding Box"), infoTitleConfig);
                        CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT}, .userData = debug_bg_data}) {
                            CLAY_TEXT(CLAY_STRING("{ x: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.x), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", y: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.y), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", width: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.width), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", height: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.height), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                        }
                        Clay_LayoutConfig *layoutConfig = selectedItem->layoutElement->layoutConfig;
                        CLAY_TEXT(CLAY_STRING("Layout Direction"), infoTitleConfig);
                        CLAY_TEXT(layoutConfig->layoutDirection == CLAY_TOP_TO_BOTTOM ? CLAY_STRING("TOP_TO_BOTTOM") : CLAY_STRING("LEFT_TO_RIGHT"), infoTextConfig);
                        CLAY_TEXT(CLAY_STRING("Padding"), infoTitleConfig);
                        CLAY({.id = CLAY_ID("ntInsp_ElementInfoPadding"), .userData = debug_bg_data}) {
                            CLAY_TEXT(CLAY_STRING("{ left: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.left), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", right: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.right), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", top: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.top), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", bottom: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.bottom), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                        }
                        CLAY_TEXT(CLAY_STRING("Child Gap"), infoTitleConfig);
                        CLAY_TEXT(cdv_int_to_string(layoutConfig->childGap), infoTextConfig);
                        CLAY_TEXT(CLAY_STRING("Child Alignment"), infoTitleConfig);
                        CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT}, .userData = debug_bg_data}) {
                            CLAY_TEXT(CLAY_STRING("{ x: "), infoTextConfig);
                            Clay_String alignX = CLAY_STRING("LEFT");
                            if (layoutConfig->childAlignment.x == CLAY_ALIGN_X_CENTER) {
                                alignX = CLAY_STRING("CENTER");
                            } else if (layoutConfig->childAlignment.x == CLAY_ALIGN_X_RIGHT) {
                                alignX = CLAY_STRING("RIGHT");
                            }
                            CLAY_TEXT(alignX, infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", y: "), infoTextConfig);
                            Clay_String alignY = CLAY_STRING("TOP");
                            if (layoutConfig->childAlignment.y == CLAY_ALIGN_Y_CENTER) {
                                alignY = CLAY_STRING("CENTER");
                            } else if (layoutConfig->childAlignment.y == CLAY_ALIGN_Y_BOTTOM) {
                                alignY = CLAY_STRING("BOTTOM");
                            }
                            CLAY_TEXT(alignY, infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                        }
                    }
                    /* Per-config-type headers + condensed bodies (one row per
                     * attached config). Full per-config bodies for SHARED +
                     * TEXT only; other types render header-only -- this is
                     * the literal-where-possible boundary documented at the
                     * function header. */
                    for (int32_t cfgIdx = 0; cfgIdx < selectedItem->layoutElement->elementConfigs.length; ++cfgIdx) {
                        Clay_ElementConfig *elementConfig = Clay__ElementConfigArraySlice_Get(&selectedItem->layoutElement->elementConfigs, cfgIdx);
                        /* Phase 56 ext perf (STRATEGY A): header pill background +
                         * border dropped -- label is rendered in the config color
                         * directly. The padded row container stays so vertical
                         * spacing matches the rest of the info pane. */
                        Clay_Color hdr_color = cdv_config_color((uint8_t)elementConfig->type);
                        const char *hdr_label = cdv_config_label((uint8_t)elementConfig->type);
                        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(outer_pad), .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                            CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(hdr_label), .chars = hdr_label}),
                                      CLAY_TEXT_CONFIG({.textColor = hdr_color, .fontSize = font_sz, .userData = debug_text_data}));
                        }
                        if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            Clay_SharedElementConfig *sharedConfig = elementConfig->config.sharedElementConfig;
                            CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data}) {
                                /* Phase 56 ext (CHUNK C, UX fix #5): info-pane
                                 * bg color uses the same swatch + hex shape as
                                 * the tree row's inline display -- the row and
                                 * the pane read the same color at a glance. */
                                CLAY_TEXT(CLAY_STRING("Background Color"), infoTitleConfig);
                                CLAY({.layout = {.childGap = 8, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(row_h - 8.0F), CLAY_SIZING_FIXED(row_h - 8.0F)}},
                                          .backgroundColor = sharedConfig->backgroundColor,
                                          .userData = debug_bg_data,
                                          .cornerRadius = CLAY_CORNER_RADIUS(4),
                                          .border = {.color = CDV_COLOR_4, .width = {1, 1, 1, 1, 0}}}) {}
                                    CLAY_TEXT(cdv_color_hex_to_string(sharedConfig->backgroundColor), infoTextConfig);
                                }
                            }
                        } else if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_TEXT) {
                            Clay_TextElementConfig *textConfig = elementConfig->config.textElementConfig;
                            CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data}) {
                                CLAY_TEXT(CLAY_STRING("Font Size"), infoTitleConfig);
                                CLAY_TEXT(cdv_int_to_string(textConfig->fontSize), infoTextConfig);
                                CLAY_TEXT(CLAY_STRING("Font ID"), infoTitleConfig);
                                CLAY_TEXT(cdv_int_to_string(textConfig->fontId), infoTextConfig);
                                CLAY_TEXT(CLAY_STRING("Letter Spacing"), infoTitleConfig);
                                CLAY_TEXT(cdv_int_to_string(textConfig->letterSpacing), infoTextConfig);
                            }
                        }
                    }
                }
            }
        }
    }
}

/* External entry point exposed in nt_ui_clay_internal.h. Forwarded from
 * nt_ui_inspector.c (which can't see Clay private types). */
void nt_ui_internal_emit_inspector_layout_extern(nt_ui_context_t *ctx) { nt_ui_internal_emit_inspector_layout(ctx); }
// #endregion
