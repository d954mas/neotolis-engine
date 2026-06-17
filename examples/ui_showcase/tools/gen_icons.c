/* Manual dev tool: regenerates the app-widget icon PNGs in examples/ui_showcase/raw/.
 * Rasterizes coverage-AA white triangles (straight alpha: RGB=255, A=coverage) so the builder
 * premultiplies them like every other raw PNG. NOT part of any CMake target -- run by hand:
 *   cc -I deps/stb examples/ui_showcase/tools/gen_icons.c -o gen_icons -lm && ./gen_icons
 * Outputs: raw/chevron_down.png (16x16), raw/arrow_right.png (12x12), raw/caret.png (14x10). */

#include <stdint.h>
#include <stdio.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define ICON_MAX_DIM 32
#define ICON_SS 4

/* dir: 0=down (chevron), 1=right (submenu arrow), 2=up (tooltip caret). Returns coverage [0,1]
 * for unit-space sample (ux,uy); inside when |across| <= width that narrows base->apex. */
static float icon_tri_cover(int dir, float ux, float uy) {
    float a;
    float b; /* a = across-axis [-1,1] from center, b = along-axis [0,1] base->apex */
    switch (dir) {
    case 1: /* right: apex at x=1, base at x=0 */
        a = uy * 2.0F - 1.0F;
        b = ux;
        break;
    case 2: /* up: apex at y=0, base at y=1 */
        a = ux * 2.0F - 1.0F;
        b = 1.0F - uy;
        break;
    default: /* down: apex at y=1, base at y=0 */
        a = ux * 2.0F - 1.0F;
        b = uy;
        break;
    }
    const float half = 1.0F - b;
    return (b >= 0.0F && b <= 1.0F && (a < 0.0F ? -a : a) <= half) ? 1.0F : 0.0F;
}

/* Rasterize a white straight-alpha coverage-AA triangle into rgba[w*h*4]. */
static void icon_raster_tri(uint8_t *rgba, int w, int h, int dir) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0F;
            for (int sy = 0; sy < ICON_SS; ++sy) {
                for (int sx = 0; sx < ICON_SS; ++sx) {
                    const float ux = ((float)x + ((float)sx + 0.5F) / (float)ICON_SS) / (float)w;
                    const float uy = ((float)y + ((float)sy + 0.5F) / (float)ICON_SS) / (float)h;
                    acc += icon_tri_cover(dir, ux, uy);
                }
            }
            const float cov = acc / (float)(ICON_SS * ICON_SS);
            uint8_t *px = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4U];
            px[0] = 255; /* straight-alpha white; builder premultiplies on pack */
            px[1] = 255;
            px[2] = 255;
            px[3] = (uint8_t)((cov * 255.0F) + 0.5F);
        }
    }
}

static int emit(const char *path, int w, int h, int dir) {
    static uint8_t buf[ICON_MAX_DIM * ICON_MAX_DIM * 4];
    icon_raster_tri(buf, w, h, dir);
    if (!stbi_write_png(path, w, h, 4, buf, w * 4)) {
        (void)fprintf(stderr, "FAILED to write %s\n", path);
        return 1;
    }
    (void)printf("wrote %s (%dx%d)\n", path, w, h);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= emit("examples/ui_showcase/raw/chevron_down.png", 16, 16, 0);
    rc |= emit("examples/ui_showcase/raw/arrow_right.png", 12, 12, 1);
    rc |= emit("examples/ui_showcase/raw/caret.png", 14, 10, 2);
    return rc;
}
