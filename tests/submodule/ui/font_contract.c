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
    nt_text_renderer_init();
    puts("font-contract-ready");
    (void)fflush(stdout);
    const float transparent[4] = {0};
    if (argc > 1 && strcmp(argv[1], "outline") == 0) {
        nt_text_renderer_set_outline(0x1p-20F, transparent);
    } else if (argc > 1 && strcmp(argv[1], "negative") == 0) {
        nt_text_renderer_set_weight(-0.25F);
    } else if (argc > 1 && strcmp(argv[1], "tiny") == 0) {
        nt_text_renderer_set_weight(0x1p-20F);
    } else if (argc > 1) {
        nt_text_renderer_set_weight(0.25F);
    } else {
        nt_text_renderer_set_weight(0.0F);
        nt_text_renderer_set_outline(0.0F, transparent);
    }
    nt_text_renderer_reset_decoration();
    nt_text_renderer_shutdown();
    nt_gfx_shutdown();
    nt_font_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
    puts("font-contract-accepted");
    return 0;
}
