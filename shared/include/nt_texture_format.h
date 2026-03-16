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

/*
 * TextureAssetHeader -- binary header prepended to texture data in NEOPAK.
 *
 * Layout (20 bytes):
 *   magic(4) + version(2) + format(2) +
 *   width(4) + height(4) + mip_count(2) + _pad(2)
 *
 * After header: raw pixel data (width * height * bpp bytes).
 * For RGBA8: bpp = 4, so pixel_data_size = width * height * 4.
 * mip_count = 1 for base level only. If > 1, mip levels follow
 * sequentially after base level (each width/2, height/2).
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;     /* NT_TEXTURE_MAGIC */
    uint16_t version;   /* NT_TEXTURE_VERSION */
    uint16_t format;    /* nt_texture_pixel_format_t */
    uint32_t width;     /* texture width in pixels */
    uint32_t height;    /* texture height in pixels */
    uint16_t mip_count; /* number of mip levels (1 = base only) */
    uint16_t _pad;      /* explicit padding */
} NtTextureAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtTextureAssetHeader) == 20, "TextureAssetHeader must be 20 bytes");

#endif /* NT_TEXTURE_FORMAT_H */
