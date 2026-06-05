/* Owns CLAY_IMPLEMENTATION + private-typed bodies; other TUs include clay.h header-only. */

/* clang-format off */
#define CLAY_IMPLEMENTATION
#include "clay.h"
/* clang-format on */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_clamp.h"
#include "math/nt_math.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"

// #region clay module-level access (thin forwarders)
void nt_ui_clay_priv_set_measure_text_cb(Clay_Dimensions (*cb)(Clay_StringSlice, Clay_TextElementConfig *, void *)) { Clay__MeasureText = cb; }

int32_t nt_ui_clay_priv_default_max_element_count(void) { return Clay__defaultMaxElementCount; }

int32_t nt_ui_clay_priv_default_max_measure_text_word_cache_count(void) { return Clay__defaultMaxMeasureTextWordCacheCount; }

void nt_ui_clay_priv_open_element(void) { Clay__OpenElement(); }

void nt_ui_clay_priv_configure_open_element(Clay_ElementDeclaration decl) { Clay__ConfigureOpenElement(decl); }

void nt_ui_clay_priv_close_element(void) { Clay__CloseElement(); }
// #endregion

// #region clay_context primitive accessors
int32_t nt_ui_clay_priv_layout_elements_length(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_layout_elements_length: clay must be non-NULL");
    return clay->layoutElements.length;
}

float nt_ui_clay_priv_layout_width(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_layout_width: clay must be non-NULL");
    return clay->layoutDimensions.width;
}

float nt_ui_clay_priv_layout_height(Clay_Context *clay) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_layout_height: clay must be non-NULL");
    return clay->layoutDimensions.height;
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

int32_t nt_ui_clay_priv_layout_index_for_id(Clay_Context *clay, uint32_t id) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_layout_index_for_id: clay must be non-NULL");
    if (id == 0U) {
        return -1;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(clay);
    Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(id);
    int32_t idx = -1;
    if (item != NULL && item->layoutElement != NULL) {
        idx = (int32_t)(item->layoutElement - clay->layoutElements.internalArray);
    }
    Clay_SetCurrentContext(saved);
    return idx;
}

int32_t nt_ui_clay_priv_hashmap_slot_for_id(Clay_Context *clay, uint32_t id) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_hashmap_slot_for_id: clay must be non-NULL");
    if (id == 0U) {
        return -1;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(clay);
    Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(id);
    int32_t slot = -1;
    if (item != NULL && item != &Clay_LayoutElementHashMapItem_DEFAULT) {
        slot = (int32_t)(item - clay->layoutElementsHashMapInternal.internalArray);
    }
    Clay_SetCurrentContext(saved);
    return slot;
}

bool nt_ui_clay_priv_bbox_for_id(Clay_Context *clay, uint32_t id, float *x, float *y, float *w, float *h) {
    NT_ASSERT(clay != NULL && "nt_ui_clay_priv_bbox_for_id: clay must be non-NULL");
    if (id == 0U) {
        return false;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(clay);
    Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(id);
    bool ok = false;
    if (item != NULL && item != &Clay_LayoutElementHashMapItem_DEFAULT) {
        *x = item->boundingBox.x;
        *y = item->boundingBox.y;
        *w = item->boundingBox.width;
        *h = item->boundingBox.height;
        ok = true;
    }
    Clay_SetCurrentContext(saved);
    return ok;
}
// #endregion

// #region inspector_internal_accessors

uint32_t nt_ui_internal_current_open_element_id(void) {
    nt_ui_context_t *ctx = nt_ui_internal_get_inframe_ctx();
    if (ctx == NULL || ctx->clay == NULL) {
        return 0U;
    }
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
    /* CLAY_TEXT appends to layoutElements but does NOT push on openLayoutElementStack. */
    Clay_Context *cc = ctx->clay;
    if (cc->layoutElements.length <= 0) {
        return 0U;
    }
    Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&cc->layoutElements, cc->layoutElements.length - 1);
    return el->id;
}

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
    /* Clay_GetElementData reads from the current context. */
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

/* Map CLAY__ELEMENT_CONFIG_TYPE_* to our exposed 8-bit mask. */
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int32_t nt_ui_internal_collect_tree_rows(const nt_ui_context_t *ctx, nt_ui_inspector_tree_row_t *out, int32_t out_cap) {
    // #region stack-init
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

    enum { STACK_CAP = 256 };
    struct {
        int32_t elem_idx;
        uint8_t depth;
        int32_t child_cursor;
    } stack[STACK_CAP];
    int32_t sp = 0;
    // #endregion

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
                // #region row-fill
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
                    sp--;
                    continue;
                }
                // #endregion
            }
            // #region child-push
            const int32_t childCount = el->childrenOrTextContent.children.length;
            if (stack[top].child_cursor < childCount) {
                int32_t child_idx = el->childrenOrTextContent.children.elements[stack[top].child_cursor];
                stack[top].child_cursor++;
                NT_ASSERT(sp < STACK_CAP && "inspector collect_tree_rows DFS stack overflow; raise STACK_CAP");
                if (sp >= STACK_CAP) {
                    sp = 0;
                    break;
                }
                if (stack[top].depth >= UINT8_MAX - 1U) {
                    continue;
                }
                stack[sp].elem_idx = child_idx;
                stack[sp].depth = (uint8_t)(stack[top].depth + 1U);
                stack[sp].child_cursor = -1;
                sp++;
            } else {
                sp--;
            }
            // #endregion
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

// #region build_tree
/* Post-EndLayout pass: composes ancestor affine + opacity into tree_baked.
 * Declaration order iteration is sufficient — Clay rejects forward parentIds. */

/* Mutex-asserted: text leaves only carry TEXT config; non-text only carry SHARED. */
static nt_ui_element_data_t *bt_scan_userdata(Clay_LayoutElement *elem) {
    nt_ui_element_data_t *shared_ud = NULL;
    nt_ui_element_data_t *text_ud = NULL;
    for (int32_t i = 0; i < elem->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&elem->elementConfigs, i);
        if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
            shared_ud = (nt_ui_element_data_t *)cfg->config.sharedElementConfig->userData;
        } else if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_TEXT) {
            text_ud = (nt_ui_element_data_t *)cfg->config.textElementConfig->userData;
        }
    }
    NT_ASSERT(!(shared_ud && text_ud) && "build_tree scan_userdata: SHARED ⊕ TEXT — Clay data-model invariant");
    return shared_ud ? shared_ud : text_ud;
}

/* Local = T(O)·T(C)·R(rot_xyz Euler)·S·T(-C); new = accum · local.
 * Pivot is bbox center (cx, cy, 0). Pure Clay Y-down (Y-flip lives in walker).
 * `accum` and `out` are mat4 (= float[4][4]); cglm supports aliasing so out may equal accum. */
static void compose_transform_level(const nt_ui_transform_t *t, float cx, float cy, mat4 accum, mat4 out) {
    mat4 m_pivot_pos;
    glm_mat4_identity(m_pivot_pos);
    glm_translate(m_pivot_pos, (vec3){cx + t->offset_x, cy + t->offset_y, t->offset_z});

    mat4 m_rot;
    glm_euler_xyz((vec3){t->rotation_x, t->rotation_y, t->rotation_z}, m_rot);

    mat4 m_scale;
    glm_mat4_identity(m_scale);
    glm_scale(m_scale, (vec3){t->scale_x, t->scale_y, t->scale_z});

    mat4 m_pivot_neg;
    glm_mat4_identity(m_pivot_neg);
    glm_translate(m_pivot_neg, (vec3){-cx, -cy, 0.0F});

    mat4 m_local;
    mat4 m_tmp;
    glm_mat4_mul(m_pivot_pos, m_rot, m_tmp);
    glm_mat4_mul(m_tmp, m_scale, m_local);
    glm_mat4_mul(m_local, m_pivot_neg, m_local);

    glm_mat4_mul(accum, m_local, out);
}

/* Floating descendants skipped (handled by outer roots loop). Text leaves have no .children. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void bt_dfs_subtree(nt_ui_context_t *ctx, Clay_Context *cc, int32_t root_elem_idx, const nt_ui_baked_xform_t *seed) {
    nt_ui_dfs_frame_t *S = ctx->tree_dfs_stack;
    int32_t sp = 0;
    NT_ASSERT(sp < NT_UI_TREE_DFS_DEPTH_CAP);
    S[sp].elem_idx = root_elem_idx;
    memcpy(S[sp].m, seed->m, sizeof seed->m);
    S[sp].opacity = seed->opacity;
    S[sp].hierarchy_depth = seed->hierarchy_depth;
    S[sp].children_cursor = 0;
    sp++;

    while (sp > 0) {
        nt_ui_dfs_frame_t *f = &S[sp - 1];
        Clay_LayoutElement *elem = Clay_LayoutElementArray_Get(&cc->layoutElements, f->elem_idx);

        if (f->children_cursor == 0) {
            nt_ui_element_data_t *ad = bt_scan_userdata(elem);
            float m_cur[16];
            memcpy(m_cur, f->m, sizeof m_cur);
            float op = f->opacity;

            if (ad != NULL) {
                if ((ad->flags & NT_UI_ELEM_FLAG_HAS_TRANSFORM) != 0U) {
                    Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(elem->id);
                    NT_ASSERT(item != &Clay_LayoutElementHashMapItem_DEFAULT && "build_tree: element's own hashmap lookup failed");
                    const float cx = item->boundingBox.x + (item->boundingBox.width * 0.5F);
                    const float cy = item->boundingBox.y + (item->boundingBox.height * 0.5F);
                    /* In-place compose — cglm supports aliasing accum == out. */
                    compose_transform_level(&ad->transform, cx, cy, (float(*)[4])m_cur, (float(*)[4])m_cur);
                }
                if ((ad->flags & NT_UI_ELEM_FLAG_HAS_OPACITY) != 0U) {
                    op *= ad->opacity;
                }
            }
            memcpy(ctx->tree_baked[f->elem_idx].m, m_cur, sizeof m_cur);
            ctx->tree_baked[f->elem_idx].opacity = op;
            ctx->tree_baked[f->elem_idx].hierarchy_depth = f->hierarchy_depth;
            memcpy(f->m, m_cur, sizeof m_cur);
            f->opacity = op;
        }

        /* Text leaves have no .children — guard before deref. */
        const bool is_text = Clay__ElementHasConfig(elem, CLAY__ELEMENT_CONFIG_TYPE_TEXT);
        const int32_t child_count = is_text ? 0 : elem->childrenOrTextContent.children.length;

        if (f->children_cursor < child_count) {
            const int32_t child_idx = elem->childrenOrTextContent.children.elements[f->children_cursor];
            f->children_cursor++;
            NT_ASSERT(sp < NT_UI_TREE_DFS_DEPTH_CAP && "build_tree: DFS stack overflow; raise NT_UI_TREE_DFS_DEPTH_CAP or restructure UI");
            /* Fail-closed in OFF builds — assert vanishes, but a pop-back would corrupt the stack. */
            if (sp >= NT_UI_TREE_DFS_DEPTH_CAP) {
                break;
            }
            S[sp].elem_idx = child_idx;
            memcpy(S[sp].m, f->m, sizeof f->m);
            S[sp].opacity = f->opacity;
            S[sp].hierarchy_depth = (uint16_t)(f->hierarchy_depth + 1U);
            S[sp].children_cursor = 0;
            sp++;
        } else {
            sp--;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_internal_build_tree(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && ctx->clay != NULL && "build_tree: ctx + clay required");
    NT_ASSERT(Clay_GetCurrentContext() == ctx->clay && "build_tree: Clay current ctx must equal ctx->clay");

    Clay_Context *cc = ctx->clay;
    const int32_t N = cc->layoutElements.length;
    const int32_t R = cc->layoutElementTreeRoots.length;

    // #region init + degenerate-early-out
    /* Covers unvisited elements (R==0, maxElementsExceeded, walker reads of stale-index errors). */
    const nt_ui_baked_xform_t identity = nt_ui_internal_identity_baked();
    for (int32_t i = 0; i < N; ++i) {
        ctx->tree_baked[i] = identity;
        /* Reset BEFORE R==0 early-return so test/debug readers don't see stale data. */
        ctx->tree_root_for_elem[i] = -1;
    }
    if (R == 0) {
        /* Bump generation so hit-test rejects stale ids that would still match. */
        ctx->current_generation++;
        return;
    }
    // #endregion

    // #region elem-to-root-map
    for (int32_t k = 0; k < R; ++k) {
        Clay__LayoutElementTreeRoot *root = Clay__LayoutElementTreeRootArray_Get(&cc->layoutElementTreeRoots, k);
        ctx->tree_root_for_elem[root->layoutElementIndex] = k;
    }
    // #endregion

    // #region seed-and-dfs
    /* Each tree root's seed = identity (root 0) or tree_baked[parentId-resolved index]. */
    for (int32_t elem_idx = 0; elem_idx < N; ++elem_idx) {
        const int32_t root_idx = ctx->tree_root_for_elem[elem_idx];
        if (root_idx < 0) {
            continue;
        }

        nt_ui_baked_xform_t seed;
        if (root_idx == 0) {
            seed = identity;
        } else {
            Clay__LayoutElementTreeRoot *root = Clay__LayoutElementTreeRootArray_Get(&cc->layoutElementTreeRoots, root_idx);
            Clay_LayoutElementHashMapItem *p_item = Clay__GetHashMapItem(root->parentId);
            if (p_item == &Clay_LayoutElementHashMapItem_DEFAULT) {
                NT_ASSERT(false && "build_tree: floating root's parentId not in hashmap (Clay error path)");
                seed = identity;
            } else {
                const int32_t p_elem_idx = (int32_t)(p_item->layoutElement - cc->layoutElements.internalArray);
                if (p_elem_idx < 0 || p_elem_idx >= N) {
                    NT_ASSERT(false && "build_tree: parent elem_idx out of bounds");
                    seed = identity;
                } else {
                    /* Floating parent must precede child in declaration order or seed would be identity. */
                    NT_ASSERT(p_elem_idx < elem_idx && "build_tree: floating parent must precede child in declaration order (Clay invariant broken)");
                    seed = ctx->tree_baked[p_elem_idx];
                    seed.hierarchy_depth = (uint16_t)(seed.hierarchy_depth + 1U);
                }
            }
        }
        bt_dfs_subtree(ctx, cc, elem_idx, &seed);
    }
    // #endregion

    // #region snapshot-per-id (hit_baked)
    /* Snapshot per-id into hashmap-slot-indexed arrays (Clay's hashmap is persistent across frames). */
    ctx->current_generation++;
    for (int32_t elem_idx = 0; elem_idx < N; ++elem_idx) {
        Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&cc->layoutElements, elem_idx);
        Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(el->id);
        if (item == &Clay_LayoutElementHashMapItem_DEFAULT) {
            continue;
        }
        const int32_t slot = (int32_t)(item - cc->layoutElementsHashMapInternal.internalArray);
        if (slot < 0 || slot >= (int32_t)ctx->max_elements) {
            continue;
        }
        ctx->hit_baked[slot] = ctx->tree_baked[elem_idx];
        const int32_t clip_id = (elem_idx < cc->layoutElementClipElementIds.length) ? Clay__int32_tArray_GetValue(&cc->layoutElementClipElementIds, elem_idx) : 0;
        ctx->hit_clip_parent_id[slot] = (uint32_t)clip_id;
        ctx->hit_generation[slot] = ctx->current_generation;
#if NT_UI_DEBUG_TOOLS
        /* Per-slot layer cache: hit-test branches inspector_view_proj vs user view_proj on this in 3D ctx. */
        Clay_LayoutElement *e = Clay_LayoutElementArray_Get(&cc->layoutElements, elem_idx);
        const nt_ui_element_data_t *ad = bt_scan_userdata(e);
        ctx->hit_layer[slot] = (ad != NULL) ? ad->layer : 0U;
#endif
    }
    // #endregion

    // #region synthetic-scissor-fixup
    /* Id mismatch flags synthetic root-wrap SCISSOR_START vs per-element SCISSOR_START. */
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            continue;
        }
        if (c->nt_layout_index < 0 || c->nt_layout_index >= N) {
            c->nt_layout_index = -1;
            continue;
        }
        Clay_LayoutElement *e = Clay_LayoutElementArray_Get(&cc->layoutElements, c->nt_layout_index);
        if (e->id != c->id) {
            c->nt_layout_index = -1;
        }
    }
    // #endregion
}
// #endregion

#ifdef NT_TEST_ACCESS
const nt_ui_baked_xform_t *nt_ui_internal_test_get_tree_baked(const nt_ui_context_t *ctx, int32_t elem_idx) {
    NT_ASSERT(ctx != NULL);
    if (elem_idx < 0 || ctx->clay == NULL || elem_idx >= ctx->clay->layoutElements.length) {
        return NULL;
    }
    return &ctx->tree_baked[elem_idx];
}

int32_t nt_ui_internal_test_get_tree_baked_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return (ctx->clay != NULL) ? ctx->clay->layoutElements.length : 0;
}

int32_t nt_ui_internal_test_get_tree_root_for_elem(const nt_ui_context_t *ctx, int32_t elem_idx) {
    NT_ASSERT(ctx != NULL);
    if (elem_idx < 0 || ctx->clay == NULL || elem_idx >= ctx->clay->layoutElements.length) {
        return -1;
    }
    return ctx->tree_root_for_elem[elem_idx];
}
#endif

#if NT_UI_DEBUG_TOOLS
// #region inspector_emit_layout
/* Port of Clay__RenderDebugView, run inside the user layout pass so it can read Clay private types. */

static const Clay_Color CDV_COLOR_1 = {58, 56, 52, 255};
static const Clay_Color CDV_COLOR_2 = {62, 60, 58, 255};
static const Clay_Color CDV_COLOR_3 = {141, 133, 135, 255};
static const Clay_Color CDV_COLOR_4 = {238, 226, 231, 255};
static const Clay_Color CDV_COLOR_SELECTED_ROW = {102, 80, 78, 255};
static const Clay_Color CDV_HIGHLIGHT_COLOR = {168, 66, 28, 100};

/* Filtered out of the viewport-hover scan to prevent self-feedback on the highlight rect. */
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
    /* Skip Clay's auto root so propagation picks a real user element. */
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

/* Unpacks 0xAABBGGRR. */
static inline Clay_Color cdv_widget_color_from_packed(uint32_t packed) {
    Clay_Color c;
    c.r = (float)(packed & 0xFFU);
    c.g = (float)((packed >> 8) & 0xFFU);
    c.b = (float)((packed >> 16) & 0xFFU);
    c.a = (float)((packed >> 24) & 0xFFU);
    return c;
}

/* CLAY_TEXT routes userData to TEXT; everything else routes to SHARED. */
static int32_t cdv_element_layer(const nt_ui_context_t *ctx, Clay_LayoutElement *el) {
    (void)ctx;
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

/* Per-frame scratch — Clay_String pointers must outlive layout solve, so we
 * never wrap; assert on overflow because wrapping would overwrite still-live
 * strings and corrupt the inspector text. Raise NT_UI_INSPECTOR_INT_BUFS if hit. */
#ifndef NT_UI_INSPECTOR_INT_BUFS
#define NT_UI_INSPECTOR_INT_BUFS 512
#endif
/* Module-level rings are SHARED across ctxs; cdv_strings_owner pins them to
 * the inspector's ctx between emit_inspector_layout and the matching walk so a
 * second ctx's emit can't overwrite still-live Clay_String pointers. */
static const nt_ui_context_t *cdv_strings_owner = NULL;
static char cdv_int_bufs[NT_UI_INSPECTOR_INT_BUFS][16];
static uint32_t cdv_int_buf_cursor = 0U;
static Clay_String cdv_int_to_string(int32_t v) {
    NT_ASSERT(cdv_int_buf_cursor < NT_UI_INSPECTOR_INT_BUFS && "inspector int-string scratch overflow; raise NT_UI_INSPECTOR_INT_BUFS");
    char *buf = cdv_int_bufs[cdv_int_buf_cursor++];
    const int n = snprintf(buf, sizeof cdv_int_bufs[0], "%d", v);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Separate ring so hex and decimal cursors don't compete. */
static char cdv_hex_bufs[NT_UI_INSPECTOR_INT_BUFS][16];
static uint32_t cdv_hex_buf_cursor = 0U;
static Clay_String cdv_hex_id_to_string(uint32_t v) {
    NT_ASSERT(cdv_hex_buf_cursor < NT_UI_INSPECTOR_INT_BUFS && "inspector hex-string scratch overflow; raise NT_UI_INSPECTOR_INT_BUFS");
    char *buf = cdv_hex_bufs[cdv_hex_buf_cursor++];
    const int n = snprintf(buf, sizeof cdv_hex_bufs[0], "#%08X", v);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Shares the hex ring's cursor space (reset per frame). */
static Clay_String cdv_color_hex_to_string(Clay_Color c) {
    NT_ASSERT(cdv_hex_buf_cursor < NT_UI_INSPECTOR_INT_BUFS && "inspector hex-string scratch overflow; raise NT_UI_INSPECTOR_INT_BUFS");
    char *buf = cdv_hex_bufs[cdv_hex_buf_cursor++];
    const uint8_t r = nt_clamp_f_to_u8(c.r);
    const uint8_t g = nt_clamp_f_to_u8(c.g);
    const uint8_t b = nt_clamp_f_to_u8(c.b);
    const uint8_t a = nt_clamp_f_to_u8(c.a);
    const int n = snprintf(buf, sizeof cdv_hex_bufs[0], "#%02X%02X%02X%02X", r, g, b, a);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

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
            /* Order-independent set. */
            ctx->inspector_collapsed_ids[i] = ctx->inspector_collapsed_ids[ctx->inspector_collapsed_count - 1U];
            ctx->inspector_collapsed_count--;
            return;
        }
    }
    if (ctx->inspector_collapsed_count < ctx->inspector_collapsed_cap) {
        ctx->inspector_collapsed_ids[ctx->inspector_collapsed_count++] = id;
    }
}

typedef struct {
    int32_t row_count;
    int32_t selected_element_row_index;
} cdv_layout_data_t;

/* Hover → ctx->inspector_highlight_id; click → ctx->inspector_selected_id. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
static cdv_layout_data_t cdv_render_layout_elements_list(nt_ui_context_t *ctx, int32_t initial_roots_length, int32_t highlighted_row_index) {
    // #region dfs-setup
    Clay_Context *context = ctx->clay;
    /* Cast types match Clay struct fields (uint16_t for Padding / fontSize). */
    const float row_h = ctx->inspector_metrics.row_height;
    const uint16_t font_sz = ctx->inspector_metrics.font_size;
    const uint16_t indent_w = (uint16_t)ctx->inspector_metrics.indent_width;
    /* Private DFS stack — avoid mutating Clay's reusableElementIndexBuffer. */
    enum { CDV_DFS_CAP = 256 };
    int32_t dfs_elems[CDV_DFS_CAP];
    bool dfs_visited[CDV_DFS_CAP];
    /* Filtered (no-identity) frames skip the open and must skip the close. */
    bool dfs_opened_wrappers[CDV_DFS_CAP];
    int32_t dfs_length = 0;
    Clay__DebugView_ScrollViewItemLayoutConfig = (Clay_LayoutConfig){.sizing = {.height = CLAY_SIZING_FIXED(row_h)}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}};
    cdv_layout_data_t layoutData = {0};
    uint32_t highlightedElementId = 0U;
    /* BG/TEXT split keeps walker batching one BG->TEXT boundary per segment. */
    void *const debug_bg_data = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_BG);
    void *const debug_text_data = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_TEXT);
    Clay_TextElementConfig debug_text_name_cfg_storage = Clay__DebugView_TextNameConfig;
    debug_text_name_cfg_storage.userData = debug_text_data;
    Clay_TextElementConfig *const debug_text_name_cfg = Clay__StoreTextElementConfig(debug_text_name_cfg_storage);
    // #endregion

    // #region row-emit
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
            const int32_t layer = cdv_element_layer(ctx, currentElement);
            const nt_ui_widget_def_t *wdef = nt_ui_widget_lookup(ctx, currentElement->id);
            Clay_String idString = context->layoutElementIdStrings.internalArray[currentElementIndex];
            /* Anonymous wrappers descend silently. */
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
                    const bool currently_collapsed = cdv_is_collapsed(ctx, currentElement->id);
                    const Clay_ElementId dotId = Clay__HashString(CLAY_STRING("ntInsp_CollapseDot"), 0, currentElement->id);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                        CLAY({.id = dotId,
                              .layout = {.sizing = {CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8)}},
                              .userData = debug_bg_data,
                              .backgroundColor = currently_collapsed ? CDV_COLOR_4 : CDV_COLOR_3,
                              .cornerRadius = currently_collapsed ? CLAY_CORNER_RADIUS(0) : CLAY_CORNER_RADIUS(2)}) {}
                    }
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
                    /* Fall back to hex id so unnamed CLAY_IDI / auto-anonymous stay identifiable. */
                    if (idString.length > 0) {
                        CLAY_TEXT(idString, offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}) : debug_text_name_cfg);
                    } else {
                        CLAY_TEXT(cdv_hex_id_to_string(currentElement->id),
                                  offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}) : debug_text_name_cfg);
                    }
                    /* Swatch only when a SHARED config with non-zero alpha is attached. */
                    for (int32_t cfgScan = 0; cfgScan < currentElement->elementConfigs.length; ++cfgScan) {
                        Clay_ElementConfig *sc = Clay__ElementConfigArraySlice_Get(&currentElement->elementConfigs, cfgScan);
                        if (sc->type != CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            continue;
                        }
                        const Clay_Color bg = sc->config.sharedElementConfig->backgroundColor;
                        if (bg.a <= 0) {
                            break;
                        }
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
                    /* No pill backgrounds — row collapses to one BG->TEXT boundary. */
                    for (int32_t elementConfigIndex = 0; elementConfigIndex < currentElement->elementConfigs.length; ++elementConfigIndex) {
                        Clay_ElementConfig *elementConfig = Clay__ElementConfigArraySlice_Get(&currentElement->elementConfigs, elementConfigIndex);
                        if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            Clay_CornerRadius radius = elementConfig->config.sharedElementConfig->cornerRadius;
                            if (radius.bottomLeft > 0) {
                                CLAY_TEXT(CLAY_STRING("Radius"), CLAY_TEXT_CONFIG({.textColor = {243, 134, 48, 255}, .fontSize = font_sz, .userData = debug_text_data}));
                            }
                            continue;
                        }
                        Clay_Color config_color = cdv_config_color((uint8_t)elementConfig->type);
                        const char *labelStr = cdv_config_label((uint8_t)elementConfig->type);
                        CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(labelStr), .chars = labelStr}),
                                  CLAY_TEXT_CONFIG({.textColor = config_color, .fontSize = font_sz, .userData = debug_text_data}));
                    }
                    if (wdef != NULL && wdef->name != NULL) {
                        Clay_Color wbg = cdv_widget_color_from_packed(wdef->pill_color);
                        CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(wdef->name), .chars = wdef->name}), CLAY_TEXT_CONFIG({.textColor = wbg, .fontSize = font_sz, .userData = debug_text_data}));
                    }
                    if (layer >= 0) {
                        CLAY_TEXT(CLAY_STRING("L:"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = font_sz, .userData = debug_text_data}));
                        CLAY_TEXT(cdv_int_to_string(layer), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = font_sz, .userData = debug_text_data}));
                    }
                }
            } /* if (has_identity) */

            /* Text-row hit-test is offset by 1 so clicking it selects the parent widget. */
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
                /* Hidden subtrees descend silently with no indent step. */
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.padding = {.left = 8}}, .userData = debug_bg_data});
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.padding = {.left = indent_w}}, .userData = debug_bg_data, .border = {.color = CDV_COLOR_3, .width = {.left = 1}}});
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data});
                dfs_opened_wrappers[dfs_length - 1] = true;
            }

            /* row_count tracks VISIBLE rows so pointer-Y → row mapping stays correct across filters. */
            if (has_identity) {
                layoutData.row_count++;
            }
            if (!Clay__ElementHasConfig(currentElement, CLAY__ELEMENT_CONFIG_TYPE_TEXT) && !cdv_is_collapsed(ctx, currentElement->id)) {
                const int32_t childLen = currentElement->childrenOrTextContent.children.length;
                int32_t *childElems = currentElement->childrenOrTextContent.children.elements;
                /* Clay__RootContainer (element 0) is still open here; children live in the in-flight buffer. */
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
    // #endregion

    // #region viewport-hover-scan
    /* No sidebar row picked → scan viewport for a user element under the cursor. */
    if (highlightedElementId == 0U && !ctx->inspector_pointer_consumed) {
        const float panel_left_x = context->layoutDimensions.width - ctx->inspector_metrics.panel_width;

        /* Transform-aware; LAST hit wins (deepest in declaration order). */
        const float px = context->pointerInfo.position.x;
        const float py = context->pointerInfo.position.y;
        for (int32_t zi = (int32_t)ctx->debug_zone_count - 1; zi >= 0; --zi) {
            const nt_ui_debug_zone_t *z = &ctx->debug_zones[zi];
            if (z->id == 0U) {
                continue;
            }
            /* Forward-transform the visual center; panel filter operates in screen space.
             * Extract 2D affine subset from the recorded mat4 (a=m[0], b=m[4], c=m[1], d=m[5]). */
            const float a = z->m[0];
            const float b = z->m[4];
            const float c = z->m[1];
            const float dd = z->m[5];
            const float tx = z->m[12];
            const float ty = z->m[13];
            const float screen_cx = (z->center_x * a) + (z->center_y * b) + tx;
            if (screen_cx >= panel_left_x) {
                continue;
            }
            const float det = (a * dd) - (b * c);
            if (det == 0.0F) {
                continue;
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
                break;
            }
        }

        /* Prefer a registered-widget id so panel/group/image/label surface over their wrappers. */
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
                /* Skip anything inside the panel footprint — the named-id set is not exhaustive. */
                if (item->boundingBox.x >= panel_left_x) {
                    continue;
                }
                if (nt_ui_widget_lookup(ctx, eid->id) != NULL) {
                    highlightedElementId = eid->id;
                    break;
                }
                if (fallback_id == 0U) {
                    fallback_id = eid->id;
                }
            }
            if (highlightedElementId == 0U && fallback_id != 0U) {
                highlightedElementId = fallback_id;
            }
        }
    }
    // #endregion

    // #region highlight-emit
    if (highlightedElementId) {
        /* zIndex 32764 keeps it above game UI but strictly under the panel root. */
        CLAY({.id = CLAY_ID("ntInsp_ElementHighlight"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
              .userData = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_HIGHLIGHT),
              .floating = {.parentId = highlightedElementId, .zIndex = 32764, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH, .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID}}) {
            CLAY({.id = CLAY_ID("ntInsp_ElementHighlightRectangle"),
                  .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                  .userData = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_HIGHLIGHT),
                  .backgroundColor = CDV_HIGHLIGHT_COLOR}) {}
        }
        ctx->inspector_highlight_id = highlightedElementId;
    } else if (ctx->inspector_selected_id != 0U) {
        /* No hover — focus the last clicked sidebar row. */
        ctx->inspector_highlight_id = ctx->inspector_selected_id;
    }
    return layoutData;
    // #endregion
}

/* Close button toggles ctx->inspector_active. Info pane carries condensed bodies for SHARED/TEXT. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_ui_internal_emit_inspector_layout(nt_ui_context_t *ctx) {
    // #region close-button
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(ctx->in_frame);
    NT_ASSERT(ctx->clay != NULL);
    /* Claim the shared cdv rings — fail loud if another ctx still holds them. */
    NT_ASSERT((cdv_strings_owner == NULL || cdv_strings_owner == ctx) && "inspector string rings owned by another ctx; complete its nt_ui_walk before opening a second inspector");
    cdv_strings_owner = ctx;

    /* Deterministic 512-slot window per frame for stable Clay_String pointers. */
    cdv_int_buf_cursor = 0U;
    cdv_hex_buf_cursor = 0U;

    const float panel_w = ctx->inspector_metrics.panel_width;
    const float row_h = ctx->inspector_metrics.row_height;
    const uint16_t font_sz = ctx->inspector_metrics.font_size;
    const uint16_t outer_pad = (uint16_t)ctx->inspector_metrics.outer_padding;

    Clay_Context *context = ctx->clay;
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
    // #endregion

    // #region scroll-setup
    uint32_t initialRootsLength = (uint32_t)context->layoutElementTreeRoots.length;
    uint32_t initialElementsLength = (uint32_t)context->layoutElements.length;
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
    // #endregion
    // #region row-list
    /* RIGHT_CENTER overlay attach — engine root is full-width so side-by-side would land off-screen. */
    CLAY({.id = CLAY_ID("ntInsp_Root"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(panel_w), CLAY_SIZING_FIXED(context->layoutDimensions.height)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .userData = NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_PANEL_BG),
          .floating = {.zIndex = 32765,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_CENTER, .parent = CLAY_ATTACH_POINT_RIGHT_CENTER},
                       .attachTo = CLAY_ATTACH_TO_ROOT,
                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT},
          .border = {.color = CDV_COLOR_3, .width = {.bottom = 1}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h)}, .padding = {outer_pad, outer_pad, 0, 0}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = CDV_COLOR_2,
              .userData = debug_bg_data}) {
            CLAY_TEXT(CLAY_STRING("nt_ui_inspector (Clay debug view port)"), infoTextConfig);
            CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}}, .userData = debug_bg_data}) {}
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
        // #endregion
        // #region selected-info-pane
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
                    /* SHARED/TEXT carry full bodies; other configs are header-only. */
                    for (int32_t cfgIdx = 0; cfgIdx < selectedItem->layoutElement->elementConfigs.length; ++cfgIdx) {
                        Clay_ElementConfig *elementConfig = Clay__ElementConfigArraySlice_Get(&selectedItem->layoutElement->elementConfigs, cfgIdx);
                        Clay_Color hdr_color = cdv_config_color((uint8_t)elementConfig->type);
                        const char *hdr_label = cdv_config_label((uint8_t)elementConfig->type);
                        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(outer_pad), .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .userData = debug_bg_data}) {
                            CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(hdr_label), .chars = hdr_label}),
                                      CLAY_TEXT_CONFIG({.textColor = hdr_color, .fontSize = font_sz, .userData = debug_text_data}));
                        }
                        if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            Clay_SharedElementConfig *sharedConfig = elementConfig->config.sharedElementConfig;
                            CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .userData = debug_bg_data}) {
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
        // #endregion
    }
}

/* Forwarded from nt_ui_inspector.c (no Clay private types there). */
void nt_ui_internal_emit_inspector_layout_extern(nt_ui_context_t *ctx) { nt_ui_internal_emit_inspector_layout(ctx); }

void nt_ui_internal_inspector_strings_release(const nt_ui_context_t *ctx) {
    if (cdv_strings_owner == ctx) {
        cdv_strings_owner = NULL;
    }
}
// #endregion
#endif /* NT_UI_DEBUG_TOOLS */
