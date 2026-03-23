#ifndef NT_TEXTURE_FORMAT_H
#define NT_TEXTURE_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "TTEX" as uint32_t little-endian = 0x58455454 */
#define NT_TEXTURE_MAGIC 0x58455454
#define NT_TEXTURE_VERSION 1

/* Texture pixel formats */
typedef enum {
    NT_TEXTURE_FORMAT_RGBA8 = 1, /* 4 bytes per pixel, 8 bits per channel */
    NT_TEXTURE_FORMAT_RGB8 = 2,  /* 3 bytes per pixel, no alpha */
    NT_TEXTURE_FORMAT_RG8 = 3,   /* 2 bytes per pixel, two channels */
    NT_TEXTURE_FORMAT_R8 = 4,    /* 1 byte per pixel, single channel */
} nt_texture_pixel_format_t;

/* Bytes per pixel for each format */
static inline uint32_t nt_texture_bpp(nt_texture_pixel_format_t fmt) {
    switch (fmt) {
    case NT_TEXTURE_FORMAT_RGBA8:
        return 4;
    case NT_TEXTURE_FORMAT_RGB8:
        return 3;
    case NT_TEXTURE_FORMAT_RG8:
        return 2;
    case NT_TEXTURE_FORMAT_R8:
        return 1;
    default:
        return 4;
    }
}

/*
 * TextureAssetHeader -- binary header prepended to texture data in ntpack.
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

/* --- Texture Asset v2 (Basis Universal compressed textures) --- */

#define NT_TEXTURE_VERSION_V2 2

/* Compression type stored in v2 header */
typedef enum {
    NT_TEXTURE_COMPRESSION_RAW = 0,   /* uncompressed pixel data (same as v1) */
    NT_TEXTURE_COMPRESSION_BASIS = 1, /* Basis Universal encoded data */
} nt_texture_compression_t;

/*
 * TextureAssetHeader v2 -- 24 bytes.
 *
 * Layout:
 *   magic(4) + version(2) + format(2) +
 *   width(4) + height(4) + mip_count(2) +
 *   compression(1) + _pad(1) + data_size(4)
 *
 * version=2 distinguishes from v1. compression field (D-08):
 *   RAW(0): uncompressed pixel data follows (like v1 but with explicit data_size)
 *   BASIS(1): Basis Universal encoded blob follows
 *
 * format field (D-10) serves double duty:
 *   RAW: pixel layout (RGBA8, RGB8, RG8, R8)
 *   BASIS: source channel config (RGBA8 = has alpha, RGB8 = no alpha)
 *
 * No mip_sizes[] array (D-09): RAW mips are calculable from dimensions,
 * BASIS mip boundaries are parsed internally by the transcoder.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      /* NT_TEXTURE_MAGIC */
    uint16_t version;    /* NT_TEXTURE_VERSION_V2 (= 2) */
    uint16_t format;     /* nt_texture_pixel_format_t (source channel config for BASIS) */
    uint32_t width;      /* base level width in pixels */
    uint32_t height;     /* base level height in pixels */
    uint16_t mip_count;  /* number of mip levels in chain */
    uint8_t compression; /* nt_texture_compression_t: 0=RAW, 1=BASIS */
    uint8_t _pad;        /* alignment padding */
    uint32_t data_size;  /* total bytes of data after this header */
} NtTextureAssetHeaderV2;
#pragma pack(pop)

_Static_assert(sizeof(NtTextureAssetHeaderV2) == 24, "v2 header must be 24 bytes");

#endif /* NT_TEXTURE_FORMAT_H */
