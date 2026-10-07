#include <stdio.h>
#include <string.h>

#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"

int main(int argc, char **argv) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    if (nt_font_init(&(nt_font_desc_t){.max_fonts = 1}) != NT_OK) {
        return 1;
    }
    nt_gfx_init(&(nt_gfx_desc_t){0});
    puts("font-contract-ready");
    (void)fflush(stdout);
    const uint32_t transparent = 0U;
    nt_text_style_t style = {.font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = 2})};
    if (argc > 1 && strcmp(argv[1], "outline") == 0) {
        style.outline_w = 0x1p-20F;
        style.outline_color = transparent;
    } else if (argc > 1 && strcmp(argv[1], "negative") == 0) {
        style.weight_em = -0.25F;
    } else if (argc > 1 && strcmp(argv[1], "tiny") == 0) {
        style.weight_em = 0x1p-20F;
    } else if (argc > 1) {
        style.weight_em = 0.25F;
    }
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_text_renderer_draw_n(&style, identity, "", 0U);
    nt_font_destroy(style.font);
    nt_text_renderer_shutdown();
    nt_gfx_shutdown();
    nt_font_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
    puts("font-contract-accepted");
    return 0;
}
