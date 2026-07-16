#include "nt_builder.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(offsetof(nt_atlas_opts_t, max_added_area_percent) > offsetof(nt_atlas_opts_t, gen_mipmaps), "atlas controls must be append-only");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, max_added_area_percent) > offsetof(nt_atlas_sprite_opts_t, extrude), "sprite controls must be append-only");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, alpha_threshold) > offsetof(nt_atlas_sprite_opts_t, max_added_area_percent), "sprite threshold must stay in the Phase-80 tail");
_Static_assert(offsetof(nt_atlas_sprite_opts_t, has_max_added_area_percent) > offsetof(nt_atlas_sprite_opts_t, alpha_threshold), "sprite presence must stay in the Phase-80 tail");

int main(void) {
    const nt_tex_compress_opts_t compress = {0};
    const nt_atlas_opts_t atlas = {
        &compress,
        NT_TEXTURE_FORMAT_RG8,
        101,
        102,
        103,
        104,
        105,
        6,
        NT_ATLAS_SHAPE_CONVEX_HULL,
        false,
        true,
        false,
        true,
        1.25F,
        NT_TEXTURE_DEFAULT_FILTER_NEAREST,
        NT_TEXTURE_DEFAULT_FILTER_NEAREST,
        NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE,
        NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE,
        false,
    };
    const nt_atlas_sprite_opts_t sprite = {
        "legacy", 0.25F, 0.75F, 11, 12, 13, 14, NT_ATLAS_SPRITE_SHAPE_CONVEX, NT_ATLAS_SPRITE_ROTATE_NO, 9, 15, 16,
    };

    if (atlas.compress != &compress || atlas.format != NT_TEXTURE_FORMAT_RG8 || atlas.max_size != 101 || atlas.padding != 102 || atlas.margin != 103 || atlas.extrude != 104 ||
        atlas.alpha_threshold != 105 || atlas.max_vertices != 6 || atlas.shape != NT_ATLAS_SHAPE_CONVEX_HULL || atlas.allow_transform || !atlas.power_of_two || atlas.debug_png ||
        !atlas.premultiplied || atlas.pixels_per_unit != 1.25F || atlas.filter_min != NT_TEXTURE_DEFAULT_FILTER_NEAREST || atlas.filter_mag != NT_TEXTURE_DEFAULT_FILTER_NEAREST ||
        atlas.wrap_u != NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE || atlas.wrap_v != NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE || atlas.gen_mipmaps || atlas.max_added_area_percent != 0.0F) {
        return 1;
    }
    if (sprite.name == NULL || sprite.name[0] != 'l' || sprite.origin_x != 0.25F || sprite.origin_y != 0.75F || sprite.slice9_left != 11 || sprite.slice9_right != 12 || sprite.slice9_top != 13 ||
        sprite.slice9_bottom != 14 || sprite.shape != NT_ATLAS_SPRITE_SHAPE_CONVEX || sprite.allow_rotate != NT_ATLAS_SPRITE_ROTATE_NO || sprite.max_vertices != 9 || sprite.margin != 15 ||
        sprite.extrude != 16 || sprite.max_added_area_percent != 0.0F || sprite.alpha_threshold != 0 || sprite.has_max_added_area_percent) {
        return 2;
    }
    return 0;
}
