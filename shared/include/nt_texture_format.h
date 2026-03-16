#ifndef NT_TEXTURE_FORMAT_H
#define NT_TEXTURE_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "TTEX" as uint32_t little-endian = 0x58455454 */
#define NT_TEXTURE_MAGIC 0x58455454
#define NT_TEXTURE_VERSION 1

/* Texture pixel formats */
typedef enum {
    NT_TEXTURE_FORMAT_RGBA8 = 1, /* 4 bytes per pixel, 8 bits per channel */
} nt_texture_pixel_format_t;

/* stub -- will be replaced */
typedef struct {
    uint32_t magic;
} NtTextureAssetHeader;

_Static_assert(sizeof(NtTextureAssetHeader) == 20,
               "TextureAssetHeader must be 20 bytes");

#endif /* NT_TEXTURE_FORMAT_H */
