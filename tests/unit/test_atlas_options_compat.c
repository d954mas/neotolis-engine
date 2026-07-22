#include "nt_builder.h"

#include <stddef.h>
#include <stdint.h>

/* Positional-initializer compatibility for atlas opts is intentionally abandoned
 * (transform control moved to a mask, so old positional literals no longer map).
 * This validates the current layout via designated initializers instead: every
 * field — including the allowed_transforms mask — round-trips, and the defaults
 * helper reports the all-transforms mask. */

/* Sprite opts keep the append-only positional contract (untouched by the mask change). */
_Static_assert(offsetof(nt_atlas_sprite_opts_t, max_added_area_percent) > offsetof(nt_atlas_sprite_opts_t, extrude), "sprite controls must be append-only");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, alpha_threshold) > offsetof(nt_atlas_sprite_opts_t, max_added_area_percent), "appended controls must stay after the legacy positional fields");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, has_max_added_area_percent) > offsetof(nt_atlas_sprite_opts_t, alpha_threshold), "appended controls must stay after the legacy positional fields");

/* Anchor the current atlas layout so the next reorder is loud, not silent. */
_Static_assert(offsetof(nt_atlas_opts_t, max_added_area_percent) > offsetof(nt_atlas_opts_t, max_vertices), "atlas v2 layout: area budget sits after max_vertices");
_Static_assert(offsetof(nt_atlas_opts_t, allowed_transforms) > offsetof(nt_atlas_opts_t, shape), "atlas v2 layout: transform mask sits after shape");
_Static_assert(offsetof(nt_atlas_opts_t, gen_mipmaps) > offsetof(nt_atlas_opts_t, wrap_v), "atlas v2 layout: controls append-only from this baseline");

int main(void) {
    const nt_tex_compress_opts_t compress = {.mode = NT_TEX_COMPRESS_ETC1S};

    const nt_atlas_opts_t atlas = {
        .compress = &compress,
        .format = NT_TEXTURE_FORMAT_RG8,
        .max_size = 101,
        .padding = 102,
        .margin = 103,
        .extrude = 104,
        .alpha_threshold = 105,
        .max_vertices = 6,
        .max_added_area_percent = 7.5F,
        .shape = NT_ATLAS_SHAPE_CONVEX_HULL,
        .allowed_transforms = NT_ATLAS_TRANSFORMS_EXPORT,
        .power_of_two = true,
        .premultiplied = true,
        .pixels_per_unit = 1.25F,
        .filter_min = NT_TEXTURE_DEFAULT_FILTER_NEAREST,
        .filter_mag = NT_TEXTURE_DEFAULT_FILTER_NEAREST,
        .wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE,
    };
    const nt_atlas_sprite_opts_t sprite = {
        .name = "legacy",
        .origin_x = 0.25F,
        .origin_y = 0.75F,
        .slice9_left = 11,
        .slice9_right = 12,
        .slice9_top = 13,
        .slice9_bottom = 14,
        .shape = NT_ATLAS_SPRITE_SHAPE_CONVEX,
        .allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY,
        .max_vertices = 9,
        .margin = 15,
        .extrude = 16,
    };

    if (atlas.compress != &compress || atlas.format != NT_TEXTURE_FORMAT_RG8 || atlas.max_size != 101 || atlas.padding != 102 || atlas.margin != 103 || atlas.extrude != 104 ||
        atlas.alpha_threshold != 105 || atlas.max_vertices != 6 || atlas.max_added_area_percent != 7.5F || atlas.shape != NT_ATLAS_SHAPE_CONVEX_HULL ||
        atlas.allowed_transforms != NT_ATLAS_TRANSFORMS_EXPORT || !atlas.power_of_two || atlas.debug_png || !atlas.premultiplied || atlas.pixels_per_unit != 1.25F ||
        atlas.filter_min != NT_TEXTURE_DEFAULT_FILTER_NEAREST || atlas.filter_mag != NT_TEXTURE_DEFAULT_FILTER_NEAREST || atlas.wrap_u != NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE ||
        atlas.wrap_v != NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE || atlas.gen_mipmaps) {
        return 1;
    }
    if (sprite.name == NULL || sprite.name[0] != 'l' || sprite.origin_x != 0.25F || sprite.origin_y != 0.75F || sprite.slice9_left != 11 || sprite.slice9_right != 12 || sprite.slice9_top != 13 ||
        sprite.slice9_bottom != 14 || sprite.shape != NT_ATLAS_SPRITE_SHAPE_CONVEX || sprite.allowed_transforms != NT_ATLAS_TRANSFORMS_IDENTITY || sprite.max_vertices != 9 || sprite.margin != 15 ||
        sprite.extrude != 16 || sprite.has_max_added_area_percent) {
        return 2;
    }

    const nt_atlas_opts_t defaults = nt_atlas_opts_defaults();
    if (defaults.allowed_transforms != NT_ATLAS_TRANSFORMS_ALL) {
        return 3;
    }
    return 0;
}
