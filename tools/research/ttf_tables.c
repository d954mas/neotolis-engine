#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint32_t ru32(const uint8_t *p) { return (uint32_t)p[0]<<24|p[1]<<16|p[2]<<8|p[3]; }

int main(void) {
    FILE *f = fopen("assets/fonts/LilitaOne-RussianChineseKo.ttf", "rb");
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *d = (uint8_t*)malloc((size_t)sz); fread(d,1,(size_t)sz,f); fclose(f);

    uint16_t num_tables = (uint16_t)(d[4]<<8|d[5]);
    printf("TTF file: %.1f MB, %d tables\n\n", sz/1048576.0, num_tables);
    printf("%-8s %12s %12s   %%\n", "Table", "Offset", "Size");
    printf("%-8s %12s %12s   --\n", "-----", "------", "----");

    uint64_t glyf_size = 0, loca_size = 0, cmap_size = 0, hmtx_size = 0, kern_size = 0, gpos_size = 0;
    for (int i = 0; i < num_tables; i++) {
        const uint8_t *e = d + 12 + i * 16;
        char tag[5] = {(char)e[0],(char)e[1],(char)e[2],(char)e[3],0};
        uint32_t off = ru32(e+8), len = ru32(e+12);
        printf("%-8s %12u %12u   %.1f%%\n", tag, off, len, 100.0*len/sz);
        if (!strcmp(tag,"glyf")) glyf_size = len;
        if (!strcmp(tag,"loca")) loca_size = len;
        if (!strcmp(tag,"cmap")) cmap_size = len;
        if (!strcmp(tag,"hmtx")) hmtx_size = len;
        if (!strcmp(tag,"kern")) kern_size = len;
        if (!strcmp(tag,"GPOS")) gpos_size = len;
    }
    printf("\n=== Key tables ===\n");
    printf("glyf (outlines):  %8.1f KB  (%.1f%%)\n", glyf_size/1024.0, 100.0*glyf_size/sz);
    printf("loca (offsets):   %8.1f KB\n", loca_size/1024.0);
    printf("cmap (charmap):   %8.1f KB\n", cmap_size/1024.0);
    printf("hmtx (metrics):   %8.1f KB\n", hmtx_size/1024.0);
    printf("kern:             %8.1f KB\n", kern_size/1024.0);
    printf("GPOS:             %8.1f KB\n", gpos_size/1024.0);
    printf("\nОстальное:        %8.1f KB\n", (sz - glyf_size - loca_size - cmap_size - hmtx_size - kern_size - gpos_size)/1024.0);

    free(d);
    return 0;
}
