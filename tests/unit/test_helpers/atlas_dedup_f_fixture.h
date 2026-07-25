#ifndef NT_TEST_ATLAS_DEDUP_F_FIXTURE_H
#define NT_TEST_ATLAS_DEDUP_F_FIXTURE_H

/* An 'F' is asymmetric under all 8 D4 orientations (an L-shape is not), so a
 * wrong relative transform cannot pass by symmetry. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "nt_builder.h"                 /* NtAtlasBuild, nt_atlas_add_raw, sprite opts */
#include "nt_atlas_format.h"            /* NT_ATLAS_XFORM_* */
#include "nt_builder_atlas_geometry.h"  /* transform_point_texel, d4_dims_after */
/* clang-format on */

/* W != H so a diagonal transform swaps dimensions and a dims mix-up is visible. */
#define NT_ATLAS_F_W 9
#define NT_ATLAS_F_H 13
#define NT_ATLAS_F_MAX_PX (NT_ATLAS_F_W * NT_ATLAS_F_H * 4)

static inline bool nt_atlas_f_alpha(uint32_t x, uint32_t y) {
    const bool stem = (x < 2U);
    const bool top_bar = (y < 2U);
    const bool mid_bar = (y >= 5U && y < 7U && x < 6U);
    return stem || top_bar || mid_bar;
}

/* Colour encodes the source texel, so a page sample identifies where it came from. */
static inline void nt_atlas_f_pixel(uint32_t x, uint32_t y, uint8_t out_rgba[4]) {
    if (!nt_atlas_f_alpha(x, y)) {
        out_rgba[0] = 0;
        out_rgba[1] = 0;
        out_rgba[2] = 0;
        out_rgba[3] = 0;
        return;
    }
    out_rgba[0] = (uint8_t)(30U + (x * 20U));
    out_rgba[1] = (uint8_t)(30U + (y * 15U));
    out_rgba[2] = 200;
    out_rgba[3] = 255;
}

/* image_t = t(base) — the same direction the relative-transform search uses. */
static inline void atlas_dedup_f_fill(uint8_t transform, uint8_t *out_px, uint16_t *out_w, uint16_t *out_h) {
    uint32_t dw = 0;
    uint32_t dh = 0;
    d4_dims_after(transform, (uint32_t)NT_ATLAS_F_W, (uint32_t)NT_ATLAS_F_H, &dw, &dh);
    memset(out_px, 0, (size_t)dw * dh * 4U);
    for (uint32_t y = 0; y < (uint32_t)NT_ATLAS_F_H; ++y) {
        for (uint32_t x = 0; x < (uint32_t)NT_ATLAS_F_W; ++x) {
            int32_t tx = 0;
            int32_t ty = 0;
            transform_point_texel((int32_t)x, (int32_t)y, transform, NT_ATLAS_F_W, NT_ATLAS_F_H, &tx, &ty);
            nt_atlas_f_pixel(x, y, out_px + ((size_t)(((uint32_t)ty * dw) + (uint32_t)tx) * 4U));
        }
    }
    *out_w = (uint16_t)dw;
    *out_h = (uint16_t)dh;
}

/* The glyph is only a valid D4 probe while this holds. */
static inline bool atlas_dedup_f_images_pairwise_distinct(void) {
    uint8_t px[8][NT_ATLAS_F_MAX_PX];
    uint16_t w[8];
    uint16_t h[8];
    for (uint8_t t = 0; t < 8; ++t) {
        atlas_dedup_f_fill(t, px[t], &w[t], &h[t]);
    }
    for (uint8_t a = 0; a < 8; ++a) {
        for (uint8_t b = (uint8_t)(a + 1U); b < 8; ++b) {
            if (w[a] != w[b] || h[a] != h[b]) {
                continue;
            }
            if (memcmp(px[a], px[b], (size_t)w[a] * h[a] * 4U) == 0) {
                return false;
            }
        }
    }
    return true;
}

/* Add `count` sprites named f_t<transform>. RECT shape gives a 4-vertex quad,
 * the only shape with an exact affine local->UV map and the QUAD_* flags. */
static inline void atlas_dedup_f_fixture_add_shaped(NtAtlasBuild *atlas, const uint8_t *transforms, uint32_t count, uint8_t sprite_mask, uint8_t sprite_dedup, uint8_t shape) {
    uint8_t px[NT_ATLAS_F_MAX_PX];
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t w = 0;
        uint16_t h = 0;
        atlas_dedup_f_fill(transforms[i], px, &w, &h);
        char name[16];
        (void)snprintf(name, sizeof(name), "f_t%u", (unsigned)transforms[i]);
        nt_atlas_sprite_opts_t opts = nt_atlas_sprite_opts_defaults();
        opts.name = name;
        opts.shape = shape;
        opts.allowed_transforms = sprite_mask;
        opts.dedup = sprite_dedup;
        nt_atlas_add_raw(atlas, px, w, h, &opts);
    }
}

static inline void atlas_dedup_f_fixture_add(NtAtlasBuild *atlas, const uint8_t *transforms, uint32_t count, uint8_t sprite_mask, uint8_t sprite_dedup) {
    atlas_dedup_f_fixture_add_shaped(atlas, transforms, count, sprite_mask, sprite_dedup, (uint8_t)NT_ATLAS_SPRITE_SHAPE_RECT);
}

/* shape 0 = atlas default (concave), for comparing a derived hull against the
 * exact D4 image of the root's hull. */
static inline void atlas_dedup_f_fixture_add_concave(NtAtlasBuild *atlas, const uint8_t *transforms, uint32_t count, uint8_t sprite_mask, uint8_t sprite_dedup) {
    atlas_dedup_f_fixture_add_shaped(atlas, transforms, count, sprite_mask, sprite_dedup, 0U);
}

#endif /* NT_TEST_ATLAS_DEDUP_F_FIXTURE_H */
