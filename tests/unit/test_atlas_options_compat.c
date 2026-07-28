#include "nt_builder.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(NT_BUILD_MAX_ATLASES == 1024, "the default stats capacity must cover the full asset ceiling");

/* Atlas opts carry NO positional-initializer contract — designated initializers
 * only. Round-trips every field (incl. allowed_transforms) and pins the
 * defaults mask to ALL. */

/* Sprite opts keep the append-only positional contract: every new control lands
 * after the complete previous field list, so recompiled positional initializers
 * keep their mapping. */
_Static_assert(offsetof(nt_atlas_sprite_opts_t, max_added_area_percent) > offsetof(nt_atlas_sprite_opts_t, extrude), "sprite controls must be append-only");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, alpha_threshold) > offsetof(nt_atlas_sprite_opts_t, max_added_area_percent), "appended controls must stay after the legacy positional fields");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, has_max_added_area_percent) > offsetof(nt_atlas_sprite_opts_t, alpha_threshold), "appended controls must stay after the legacy positional fields");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, dedup) > offsetof(nt_atlas_sprite_opts_t, has_alpha_threshold), "the dedup override is appended, not inserted");

/* Anchor the current atlas layout so the next reorder is loud, not silent. */
_Static_assert(offsetof(nt_atlas_opts_t, max_added_area_percent) > offsetof(nt_atlas_opts_t, max_vertices), "atlas v2 layout: area budget sits after max_vertices");
_Static_assert(offsetof(nt_atlas_opts_t, allowed_transforms) > offsetof(nt_atlas_opts_t, shape), "atlas v2 layout: transform mask sits after shape");
_Static_assert(offsetof(nt_atlas_opts_t, gen_mipmaps) > offsetof(nt_atlas_opts_t, wrap_v), "atlas v2 layout: controls append-only from this baseline");
_Static_assert(offsetof(nt_atlas_opts_t, dedup) > offsetof(nt_atlas_opts_t, gen_mipmaps), "atlas v2 layout: dedup flag is appended");

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
        .allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY_ROT90,
        .power_of_two = true,
        .premultiplied = true,
        .pixels_per_unit = 1.25F,
        .filter_min = NT_TEXTURE_DEFAULT_FILTER_NEAREST,
        .filter_mag = NT_TEXTURE_DEFAULT_FILTER_NEAREST,
        .wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE,
        .dedup = true,
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
        .dedup = NT_ATLAS_SPRITE_DEDUP_ON,
    };

    if (atlas.compress != &compress || atlas.format != NT_TEXTURE_FORMAT_RG8 || atlas.max_size != 101 || atlas.padding != 102 || atlas.margin != 103 || atlas.extrude != 104 ||
        atlas.alpha_threshold != 105 || atlas.max_vertices != 6 || atlas.max_added_area_percent != 7.5F || atlas.shape != NT_ATLAS_SHAPE_CONVEX_HULL ||
        atlas.allowed_transforms != NT_ATLAS_TRANSFORMS_IDENTITY_ROT90 || !atlas.power_of_two || atlas.debug_png || !atlas.premultiplied || atlas.pixels_per_unit != 1.25F ||
        atlas.filter_min != NT_TEXTURE_DEFAULT_FILTER_NEAREST || atlas.filter_mag != NT_TEXTURE_DEFAULT_FILTER_NEAREST || atlas.wrap_u != NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE ||
        atlas.wrap_v != NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE || atlas.gen_mipmaps || !atlas.dedup) {
        return 1;
    }
    if (sprite.name == NULL || sprite.name[0] != 'l' || sprite.origin_x != 0.25F || sprite.origin_y != 0.75F || sprite.slice9_left != 11 || sprite.slice9_right != 12 || sprite.slice9_top != 13 ||
        sprite.slice9_bottom != 14 || sprite.shape != NT_ATLAS_SPRITE_SHAPE_CONVEX || sprite.allowed_transforms != NT_ATLAS_TRANSFORMS_IDENTITY || sprite.max_vertices != 9 || sprite.margin != 15 ||
        sprite.extrude != 16 || sprite.dedup != NT_ATLAS_SPRITE_DEDUP_ON || sprite.has_max_added_area_percent) {
        return 2;
    }

    const nt_atlas_opts_t defaults = nt_atlas_opts_defaults();
    if (defaults.allowed_transforms != NT_ATLAS_TRANSFORMS_ALL || !defaults.dedup) {
        return 3;
    }

    /* Dedup is opt-out at atlas level and opt-in per sprite, so a zero-init
     * atlas dedups nothing while a zero-init sprite inherits. */
    const nt_atlas_opts_t zero = {0};
    if (zero.dedup || ((nt_atlas_sprite_opts_t){0}).dedup != 0) {
        return 5;
    }

    /* Positional tripwire: sprite opts promise positional source compatibility,
     * so the FULL field list is initialized positionally — reordering any field
     * lands a value in the wrong slot and fails the checks below. */
    const nt_atlas_sprite_opts_t sprite_positional = {
        "legacy", 0.25F, 0.75F, 11, 12, 13, 14, NT_ATLAS_SPRITE_SHAPE_CONVEX, NT_ATLAS_TRANSFORMS_IDENTITY, 9, 15, 16, 2.5F, 7, true, true, NT_ATLAS_SPRITE_DEDUP_OFF,
    };
    if (sprite_positional.name[0] != 'l' || sprite_positional.origin_x != 0.25F || sprite_positional.origin_y != 0.75F || sprite_positional.slice9_left != 11 || sprite_positional.slice9_right != 12 ||
        sprite_positional.slice9_top != 13 || sprite_positional.slice9_bottom != 14 || sprite_positional.shape != NT_ATLAS_SPRITE_SHAPE_CONVEX ||
        sprite_positional.allowed_transforms != NT_ATLAS_TRANSFORMS_IDENTITY || sprite_positional.max_vertices != 9 || sprite_positional.margin != 15 || sprite_positional.extrude != 16 ||
        sprite_positional.dedup != NT_ATLAS_SPRITE_DEDUP_OFF || sprite_positional.max_added_area_percent != 2.5F || sprite_positional.alpha_threshold != 7 ||
        !sprite_positional.has_max_added_area_percent || !sprite_positional.has_alpha_threshold) {
        return 4;
    }
    return 0;
}
