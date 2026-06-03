/* Phase 57a-08: post-EndLayout build pass tests.
 *
 * Verifies nt_ui_internal_build_tree composes accum + opacity correctly down
 * the Clay element tree, seeds floating roots from parentId (Option C), and
 * marks synthetic SCISSOR_START commands with nt_layout_index = -1.
 *
 * Tests use NT_TEST_ACCESS accessors (tree_baked / tree_root_for_elem) to
 * inspect the populated state. The walker is NOT exercised here — its switch
 * to tree_baked lives in 57b/57c. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define SCREEN_W 800.0F
#define SCREEN_H 600.0F
#define DEG2RAD(d) ((d) * 3.14159265358979323846F / 180.0F)

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

// #region helpers
/* Tests probe tree_baked by scanning all entries and matching on rotation /
 * opacity values (one-to-one because each test uses distinct, non-colliding
 * non-identity values). No need for an id→elem_idx accessor. */
static bool near_eq(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static bool baked_is_identity(const nt_ui_baked_xform_t *bk) {
    if (bk == NULL) {
        return false;
    }
    return near_eq(bk->a, 1.0F, 1e-5F) && near_eq(bk->b, 0.0F, 1e-5F) && near_eq(bk->c, 0.0F, 1e-5F) && near_eq(bk->d, 1.0F, 1e-5F) && near_eq(bk->tx, 0.0F, 1e-5F) && near_eq(bk->ty, 0.0F, 1e-5F) &&
           near_eq(bk->opacity, 1.0F, 1e-5F);
}

// #endregion

// #region tests

/* ---- 1. Empty layout — only auto root container; tree_baked[0] = identity. ---- */
static void test_identity_root(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);

    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(1, N);
    const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, 0);
    TEST_ASSERT_NOT_NULL(bk);
    TEST_ASSERT_TRUE_MESSAGE(baked_is_identity(bk), "root container baked must be identity");
    /* Root container is layoutElementTreeRoots[0]. */
    TEST_ASSERT_EQUAL_INT32(0, nt_ui_internal_test_get_tree_root_for_elem(s_fx.ctx, 0));
}

/* ---- 2. Single CLAY child with HAS_TRANSFORM (rotation 30deg) — baked composes. ---- */
static void test_single_xform_root(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.rotation = DEG2RAD(30.0F);
    CLAY({.id = CLAY_ID("rot_container"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 100.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}},
          .userData = (void *)NT_UI_DATA_XFORM(0, &t, 1.0F)}) {}
    nt_ui_end(s_fx.ctx);

    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(2, N);
    /* rot_container is a floating root; its index is somewhere in [1, N). The
     * exact slot depends on Clay's allocation order. Find it by scanning all
     * baked entries: only one should have non-identity rotation. */
    bool found = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (!baked_is_identity(bk) && fabsf(bk->rotation - DEG2RAD(30.0F)) <= 1e-3F) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "expected to find a baked entry with rotation 30deg");
}

/* ---- 3. Two stacked transforms — leaf inherits composed rotation. ---- */
static void test_chain_xform_2level(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t outer = nt_ui_transform_defaults();
    outer.rotation = DEG2RAD(10.0F);
    nt_ui_transform_t inner = nt_ui_transform_defaults();
    inner.rotation = DEG2RAD(20.0F);
    CLAY({.id = CLAY_ID("outer"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(200.0F)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .userData = (void *)NT_UI_DATA_XFORM(0, &outer, 1.0F)}) {
        CLAY({.id = CLAY_ID("inner"), .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &inner, 1.0F)}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* Find inner — its baked.rotation should be 30deg (10 + 20 composed). */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool found_inner = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (fabsf(bk->rotation - DEG2RAD(30.0F)) <= 1e-3F) {
            found_inner = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_inner, "inner leaf must inherit composed rotation 10+20=30deg");
}

/* ---- 4. Opacity inheritance — leaf opacity = parent * own. ---- */
static void test_opacity_inheritance_2level(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t id_xform = nt_ui_transform_defaults();
    CLAY({.id = CLAY_ID("p"), .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(200.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &id_xform, 0.5F)}) {
        CLAY({.id = CLAY_ID("c"), .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &id_xform, 0.4F)}) {}
    }
    nt_ui_end(s_fx.ctx);

    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    /* Inner opacity = 0.5 * 0.4 = 0.2. Outer = 0.5. Find both. */
    bool found_inner = false, found_outer = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (near_eq(bk->opacity, 0.20F, 1e-3F)) {
            found_inner = true;
        } else if (near_eq(bk->opacity, 0.50F, 1e-3F)) {
            found_outer = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_outer, "outer opacity 0.5 not found");
    TEST_ASSERT_TRUE_MESSAGE(found_inner, "inner opacity 0.5*0.4=0.2 not found");
}

/* ---- 5. Floating ATTACH_TO_ROOT → identity seed (parent rotation NOT inherited).
 *
 *  Also verifies the lexical parent `wrap` DID compose its own 45deg into
 *  tree_baked — guards against a regression that disables non-floating subtree
 *  DFS (would let floating identity pass while breaking main-tree composition).
 */
static void test_floating_attach_to_root_identity_seed(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t rot = nt_ui_transform_defaults();
    rot.rotation = DEG2RAD(45.0F);
    CLAY({.id = CLAY_ID("wrap"), .layout = {.sizing = {CLAY_SIZING_FIXED(400.0F), CLAY_SIZING_FIXED(400.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &rot, 1.0F)}) {
        CLAY({.id = CLAY_ID("tooltip"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 100.0F}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* tooltip's baked must be identity (ATTACH_TO_ROOT means its parent in the
     * tree is root container → tree_baked[0] = identity → tooltip identity).
     * Scan all floating roots; at least one should be identity. */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    int32_t tooltip_idx = -1;
    bool found_wrap_45 = false;
    for (int32_t i = 1; i < N; ++i) {
        const int32_t root_idx = nt_ui_internal_test_get_tree_root_for_elem(s_fx.ctx, i);
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (root_idx > 0 && baked_is_identity(bk)) {
            /* Floating root with identity baked = tooltip. */
            tooltip_idx = i;
        } else if (root_idx < 0 && fabsf(bk->rotation - DEG2RAD(45.0F)) <= 1e-3F) {
            /* Non-root element with 45deg = wrap (composed its own xform via main-tree DFS). */
            found_wrap_45 = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(tooltip_idx >= 0, "ATTACH_TO_ROOT floating with parent-rotation should have identity baked");
    TEST_ASSERT_TRUE_MESSAGE(found_wrap_45, "lexical parent (wrap) should have composed 45deg via main-tree DFS");
}

/* ---- 6. Floating ATTACH_TO_PARENT → inherits lexical parent's xform. ---- */
static void test_floating_attach_to_parent_inherits_xform(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t rot = nt_ui_transform_defaults();
    rot.rotation = DEG2RAD(45.0F);
    CLAY({.id = CLAY_ID("card"), .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(200.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &rot, 1.0F)}) {
        CLAY(
            {.id = CLAY_ID("badge"), .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .offset = {.x = 10.0F, .y = 10.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(40.0F), CLAY_SIZING_FIXED(40.0F)}}}) {
        }
    }
    nt_ui_end(s_fx.ctx);

    /* badge has no own xform but its parent is card (45deg). Its tree_baked
     * rotation should be 45deg. Scan floating roots. */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool found = false;
    for (int32_t i = 1; i < N; ++i) {
        const int32_t root_idx = nt_ui_internal_test_get_tree_root_for_elem(s_fx.ctx, i);
        if (root_idx > 0) {
            const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
            if (fabsf(bk->rotation - DEG2RAD(45.0F)) <= 1e-3F) {
                found = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "ATTACH_TO_PARENT floating should inherit lexical parent's 45deg rotation");
}

/* ---- 7. Floating ATTACH_TO_ELEMENT_WITH_ID → inherits target element's xform. ---- */
static void test_floating_attach_to_element_with_id_inherits_xform(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t rot = nt_ui_transform_defaults();
    rot.rotation = DEG2RAD(15.0F);
    CLAY({.id = CLAY_ID("anchor"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT},
          .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}},
          .userData = (void *)NT_UI_DATA_XFORM(0, &rot, 1.0F)}) {}
    CLAY({.id = CLAY_ID("attached"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = CLAY_ID("anchor").id},
          .layout = {.sizing = {CLAY_SIZING_FIXED(20.0F), CLAY_SIZING_FIXED(20.0F)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* attached has no own xform; should inherit anchor's 15deg. */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    int32_t rot_count = 0;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (fabsf(bk->rotation - DEG2RAD(15.0F)) <= 1e-3F) {
            rot_count++;
        }
    }
    /* Both anchor (own xform) and attached (inherit) should show 15deg. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(2, rot_count);
}

/* ---- 8. Floating chain — A's xform propagates to B (attached to A). ---- */
static void test_floating_inside_floating_chain(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t rotA = nt_ui_transform_defaults();
    rotA.rotation = DEG2RAD(10.0F);
    nt_ui_transform_t rotB = nt_ui_transform_defaults();
    rotB.rotation = DEG2RAD(20.0F);
    CLAY({.id = CLAY_ID("A"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT},
          .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}},
          .userData = (void *)NT_UI_DATA_XFORM(0, &rotA, 1.0F)}) {}
    CLAY({.id = CLAY_ID("B"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID, .parentId = CLAY_ID("A").id},
          .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}},
          .userData = (void *)NT_UI_DATA_XFORM(0, &rotB, 1.0F)}) {}
    nt_ui_end(s_fx.ctx);

    /* B should have composed rotation 10+20 = 30deg. */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool found = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (fabsf(bk->rotation - DEG2RAD(30.0F)) <= 1e-3F) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "floating chain A→B should produce composed rotation 10+20=30deg");
}

/* ---- 9. Floating ATTACH_TO_PARENT with OWN xform — composed on seed. ---- */
static void test_floating_with_own_xform_composes_on_seed(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t outer = nt_ui_transform_defaults();
    outer.rotation = DEG2RAD(10.0F);
    nt_ui_transform_t inner = nt_ui_transform_defaults();
    inner.rotation = DEG2RAD(20.0F);
    CLAY({.id = CLAY_ID("outer"), .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(200.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &outer, 1.0F)}) {
        CLAY({.id = CLAY_ID("float"),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT},
              .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}},
              .userData = (void *)NT_UI_DATA_XFORM(0, &inner, 1.0F)}) {}
    }
    nt_ui_end(s_fx.ctx);

    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool found_30 = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (fabsf(bk->rotation - DEG2RAD(30.0F)) <= 1e-3F) {
            found_30 = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_30, "floating's own xform composes on top of parent seed: 10+20=30deg");
}

/* ---- 10. NT_UI_DATA_LAYER singleton — flags=0, no compose. ---- */
static void test_user_data_singleton_layer_only(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("plain"), .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}}, .userData = NT_UI_CLAY_DATA(5)}) {}
    nt_ui_end(s_fx.ctx);

    /* Layer-only userData should produce identity baked for the element. */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool any_non_identity = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (!baked_is_identity(bk)) {
            any_non_identity = true;
            break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(any_non_identity, "layer-only userData must keep tree_baked at identity");
}

/* ---- 11. NT_UI_DATA_XFORM macro copies transform by value (caller stack lifetime). ---- */
static void test_user_data_xform_macro_copies_transform_by_value(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    {
        nt_ui_transform_t t = nt_ui_transform_defaults();
        t.rotation = DEG2RAD(25.0F);
        CLAY({.id = CLAY_ID("xformed"), .layout = {.sizing = {CLAY_SIZING_FIXED(60.0F), CLAY_SIZING_FIXED(60.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(3, &t, 0.7F)}) {}
        /* `t` goes out of scope here; the scratch-alloc copy keeps it alive. */
    }
    nt_ui_end(s_fx.ctx);

    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool found_xform = false;
    bool found_opacity = false;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (fabsf(bk->rotation - DEG2RAD(25.0F)) <= 1e-3F) {
            found_xform = true;
        }
        if (near_eq(bk->opacity, 0.7F, 1e-3F)) {
            found_opacity = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_xform, "XFORM macro must preserve rotation");
    TEST_ASSERT_TRUE_MESSAGE(found_opacity, "XFORM macro must preserve opacity");
}

/* ---- 12. Singleton table: every slot has opacity=1.0F, flags=0. ---- */
static void test_singleton_opacity_default(void) {
    const nt_ui_element_data_t *d0 = nt_ui_make_element_data(0, NULL);
    const nt_ui_element_data_t *d128 = nt_ui_make_element_data(128, NULL);
    const nt_ui_element_data_t *d255 = nt_ui_make_element_data(255, NULL);
    TEST_ASSERT_TRUE(near_eq(d0->opacity, 1.0F, 1e-6F));
    TEST_ASSERT_TRUE(near_eq(d128->opacity, 1.0F, 1e-6F));
    TEST_ASSERT_TRUE(near_eq(d255->opacity, 1.0F, 1e-6F));
    TEST_ASSERT_EQUAL_UINT8(0, d0->flags);
    TEST_ASSERT_EQUAL_UINT8(0, d128->flags);
    TEST_ASSERT_EQUAL_UINT8(0, d255->flags);
}

/* ---- 13. Synthetic SCISSOR_START marked with nt_layout_index = -1. ----
 *
 * Setup: parent with clip config, child floating with clipTo=ATTACHED_PARENT.
 * Clay emits a synthetic SCISSOR_START at root scope (clay.h:2709-2715) before
 * the floating subtree's render. Build pass's post-process detects mismatch
 * (cmd.id != layoutElements[idx].id) and sets nt_layout_index = -1. */
static void test_synthetic_scissor_start_marked(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("clip_parent"), .layout = {.sizing = {CLAY_SIZING_FIXED(300.0F), CLAY_SIZING_FIXED(300.0F)}}, .clip = {.horizontal = true, .vertical = true}}) {
        CLAY({.id = CLAY_ID("the_float"),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT},
              .layout = {.sizing = {CLAY_SIZING_FIXED(50.0F), CLAY_SIZING_FIXED(50.0F)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    /* Scan SCISSOR_START commands. There should be BOTH:
     *  (a) at least one synthetic (root-wrap, marked nt_layout_index = -1)
     *  (b) at least one per-element (clip_parent's own clip, nt_layout_index >= 0)
     * Catches a regression that inverts the heuristic and marks ALL scissors
     * as synthetic. */
    int32_t synthetic_count = 0;
    int32_t per_element_count = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            continue;
        }
        if (c->nt_layout_index < 0) {
            synthetic_count++;
        } else {
            per_element_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(1, synthetic_count);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(1, per_element_count, "per-element SCISSOR_START from CLAY({.clip}) must survive with nt_layout_index >= 0");
}

/* ---- 14. Empty layout — tree_baked[0] still identity (no game CLAY blocks). ---- */
static void test_tree_baked_identity_init_on_empty_layout(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);

    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(1, N);
    /* All live elements should be identity (no userData attached). */
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        TEST_ASSERT_TRUE_MESSAGE(baked_is_identity(bk), "empty layout: every live element baked must be identity");
    }
}

/* ---- 15. min_arena_size includes tree storage — tight delta check.
 *
 *  Compare min_arena_size at two `max_elements` values. The delta must include
 *  per-element growth for tree_baked (40B) + tree_root_for_elem (4B), times the
 *  element count difference. tree_dfs_stack is fixed-size and doesn't scale.
 *
 *  A regression that drops one of these arrays from min_arena_size would shrink
 *  the delta below this lower bound and fire the assert. */
static void test_min_arena_size_includes_tree_storage(void) {
    nt_ui_create_desc_t desc_small = nt_ui_create_desc_defaults();
    desc_small.max_elements = 256U;
    nt_ui_create_desc_t desc_large = nt_ui_create_desc_defaults();
    desc_large.max_elements = 1024U;

    const size_t sz_small = nt_ui_min_arena_size(&desc_small);
    const size_t sz_large = nt_ui_min_arena_size(&desc_large);
    TEST_ASSERT_GREATER_THAN_size_t(sz_small, sz_large);

    /* Per-element bytes that grow with max_elements (sum across all arrays
     * that scale): markers (24 × 2 cap) + walker_baked (40) + walker_sorted ×2 (8)
     * + tree_baked (40) + tree_root_for_elem (4) = 92 + 48 = 140 B per element
     * (markers cap is max_markers=max_elements*2 by default → 24 × 2 = 48). So
     * delta for 768 extra elements ≥ 140 × 768 = 107520 B (before cache-line
     * alignment, which can only INCREASE it). Lower bound is robust to formula
     * tweaks because we're checking a delta, not absolute. */
    const size_t per_elem_bytes = (24U * 2U) + 40U + (4U * 2U) + 40U + 4U;
    const size_t expected_delta_floor = per_elem_bytes * (1024U - 256U);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(expected_delta_floor, sz_large - sz_small);
}

/* ---- 17. Multi-ctx: each ctx gets its own tree storage pointers (no aliasing). ----
 *
 *  Creates a second nt_ui_context_t in a separate arena and verifies that the
 *  test-access accessors return baked entries that diverge once one ctx runs a
 *  layout with HAS_TRANSFORM while the other stays empty. If storage aliased,
 *  both ctxes would observe the same baked values. */
static void test_multi_ctx_tree_storage_isolated(void) {
    /* Spin up a second ctx without re-running the fixture (which would shut
     * down the modules s_fx depends on). Use a separate arena slot. */
    static alignas(NT_UI_ARENA_ALIGN) uint8_t s_arena_b[NT_UI_TEST_ARENA_SIZE];
    const nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    nt_ui_context_t *ctx_b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &desc);
    TEST_ASSERT_NOT_NULL(ctx_b);
    nt_ui_set_atlas_white_region(ctx_b, s_fx.atlas.handle, s_fx.atlas.white_region_idx);
    nt_ui_set_sprite_material(ctx_b, s_fx.sprite_material);
    nt_ui_set_text_material(ctx_b, s_fx.text_material);
    nt_ui_set_font(ctx_b, 0U, s_fx.stub_font);

    /* ctx A: declare a rotated container. ctx B: empty layout. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_transform_t rot = nt_ui_transform_defaults();
    rot.rotation = DEG2RAD(60.0F);
    CLAY({.id = CLAY_ID("ctx_a_rot"), .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}}, .userData = (void *)NT_UI_DATA_XFORM(0, &rot, 1.0F)}) {}
    nt_ui_end(s_fx.ctx);

    nt_ui_begin(ctx_b, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_end(ctx_b);

    /* ctx A has at least one element with 60deg rotation. */
    const int32_t Na = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    bool a_has_60 = false;
    for (int32_t i = 0; i < Na; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (fabsf(bk->rotation - DEG2RAD(60.0F)) <= 1e-3F) {
            a_has_60 = true;
            break;
        }
    }
    /* ctx B has no element with 60deg rotation. */
    const int32_t Nb = nt_ui_internal_test_get_tree_baked_count(ctx_b);
    bool b_has_60 = false;
    for (int32_t i = 0; i < Nb; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(ctx_b, i);
        if (fabsf(bk->rotation - DEG2RAD(60.0F)) <= 1e-3F) {
            b_has_60 = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(a_has_60, "ctx A should have a 60deg-rotated baked entry");
    TEST_ASSERT_FALSE_MESSAGE(b_has_60, "ctx B (empty layout) must NOT see ctx A's 60deg — storage aliasing");

    nt_ui_destroy_context(ctx_b);
}

/* ---- 16. last_build_tree_ms records timing + perf budget. ----
 *
 *  Builds a layout with ~100 elements (well within typical UI complexity) and
 *  asserts the build pass completes under 5 ms on native-debug. A regression
 *  that re-introduces an O(N²) walk or radix sort would fire this alarm. */
static void test_build_tree_ms_recorded_and_under_budget(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    /* 100 nested boxes + 1 root = 101 elements. Realistic UI bound. */
    enum { N_BOXES = 100 };
    for (int32_t i = 0; i < N_BOXES; ++i) {
        CLAY({.id = CLAY_IDI("box", i), .layout = {.sizing = {CLAY_SIZING_FIXED(20.0F), CLAY_SIZING_FIXED(20.0F)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    const float ms = nt_ui_get_last_build_tree_ms(s_fx.ctx);
    /* Field populated (not garbage). */
    TEST_ASSERT_TRUE_MESSAGE(ms >= 0.0F && ms < 1000.0F, "build_tree_ms outside [0, 1000] — likely uninit/garbage");
    /* Native-debug budget: 5 ms for 100 elements. O(N) DFS over N=101 with a
     * single compose_transform_level call per element should run in microseconds
     * on any reasonable hardware. 5 ms is a 1000× margin for slow CI / valgrind. */
    TEST_ASSERT_TRUE_MESSAGE(ms < 5.0F, "build_tree exceeded 5 ms perf budget for 100-element layout — likely O(N²) regression");
}

#if NT_UI_DEBUG_TOOLS
// #region inspector_active tests

/* ---- 18. Inspector-active layout: build_tree completes without crashing,
 *         and every inspector-emitted element has identity baked.
 *
 *  Inspector emit_layout adds dozens of CLAY blocks with
 *  NT_UI_CLAY_DATA(NT_UI_LAYER_DEBUG_*) — singleton element_data with flags=0.
 *  Build pass should write identity to all of them. This guards a regression
 *  where inspector elements somehow pick up a composed transform (e.g. a stray
 *  XFORM on an inspector wrapper). */
static void test_inspector_subtree_baked_identity(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    /* No user CLAY blocks — let the inspector emit alone. */
    nt_ui_end(s_fx.ctx);

    /* Inspector emits many elements; tree_baked.length should now exceed the
     * empty-layout baseline. */
    const int32_t N = nt_ui_internal_test_get_tree_baked_count(s_fx.ctx);
    TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(10, N, "inspector_active should emit many CLAY elements");

    /* All entries identity — inspector userData is layer-only singletons. */
    int32_t non_identity = 0;
    for (int32_t i = 0; i < N; ++i) {
        const nt_ui_baked_xform_t *bk = nt_ui_internal_test_get_tree_baked(s_fx.ctx, i);
        if (!baked_is_identity(bk)) {
            non_identity++;
        }
    }
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, non_identity, "every inspector-emitted element should have identity baked");
}

/* ---- 19. Inspector-active synthetic SCISSOR handling: inspector's scroll panel
 *         uses ATTACH_TO_PARENT + CLIP_TO_ATTACHED_PARENT inside a CLIP scope,
 *         producing real synthetic SCISSOR_START commands. Build pass must mark
 *         them with nt_layout_index = -1 without crashing or corrupting other
 *         commands. */
static void test_inspector_active_synthetic_scissor_handled(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_end(s_fx.ctx);

    /* At least one synthetic SCISSOR_START (from inspector's CLIP_TO_ATTACHED_PARENT
     * floating panel). Plus at least one per-element SCISSOR_START. */
    int32_t synthetic_count = 0;
    int32_t per_element_count = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            continue;
        }
        if (c->nt_layout_index < 0) {
            synthetic_count++;
        } else {
            per_element_count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(1, synthetic_count, "inspector's clipTo=ATTACHED_PARENT floating should emit at least one synthetic SCISSOR_START");
    TEST_ASSERT_GREATER_OR_EQUAL_INT32_MESSAGE(1, per_element_count, "inspector's CLIP element (scroll pane) should emit a per-element SCISSOR_START");
}

// #endregion
#endif /* NT_UI_DEBUG_TOOLS */

// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_identity_root);
    RUN_TEST(test_single_xform_root);
    RUN_TEST(test_chain_xform_2level);
    RUN_TEST(test_opacity_inheritance_2level);
    RUN_TEST(test_floating_attach_to_root_identity_seed);
    RUN_TEST(test_floating_attach_to_parent_inherits_xform);
    RUN_TEST(test_floating_attach_to_element_with_id_inherits_xform);
    RUN_TEST(test_floating_inside_floating_chain);
    RUN_TEST(test_floating_with_own_xform_composes_on_seed);
    RUN_TEST(test_user_data_singleton_layer_only);
    RUN_TEST(test_user_data_xform_macro_copies_transform_by_value);
    RUN_TEST(test_singleton_opacity_default);
    RUN_TEST(test_synthetic_scissor_start_marked);
    RUN_TEST(test_tree_baked_identity_init_on_empty_layout);
    RUN_TEST(test_min_arena_size_includes_tree_storage);
    RUN_TEST(test_multi_ctx_tree_storage_isolated);
    RUN_TEST(test_build_tree_ms_recorded_and_under_budget);
#if NT_UI_DEBUG_TOOLS
    RUN_TEST(test_inspector_subtree_baked_identity);
    RUN_TEST(test_inspector_active_synthetic_scissor_handled);
#endif
    return UNITY_END();
}
