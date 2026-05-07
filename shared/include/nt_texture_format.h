#ifndef NT_TEXTURE_FORMAT_H
#define NT_TEXTURE_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "TTEX" as uint32_t little-endian = 0x58455454 */
#define NT_TEXTURE_MAGIC 0x58455454
#define NT_TEXTURE_VERSION 3

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

/* Compression type */
typedef enum {
    NT_TEXTURE_COMPRESSION_RAW = 0,   /* uncompressed pixel data */
    NT_TEXTURE_COMPRESSION_BASIS = 1, /* Basis Universal encoded data */
} nt_texture_compression_t;

/* Texture header flags (single byte, room for future bits: sRGB, linear, ...) */
#define NT_TEXTURE_FLAG_PREMULTIPLIED (1U << 0) /* RGB already multiplied by alpha */
#define NT_TEXTURE_FLAG_GEN_MIPMAPS (1U << 1)   /* runtime should glGenerateMipmap on RAW upload (no effect on BASIS) */
/* bits 2..7 reserved */

/* Values must match nt_texture_filter_t / nt_texture_wrap_t in nt_gfx.h. */
typedef enum {
    NT_TEXTURE_DEFAULT_FILTER_NEAREST = 0,
    NT_TEXTURE_DEFAULT_FILTER_LINEAR = 1,
    NT_TEXTURE_DEFAULT_FILTER_NEAREST_MIPMAP_NEAREST = 2,
    NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_NEAREST = 3,
    NT_TEXTURE_DEFAULT_FILTER_NEAREST_MIPMAP_LINEAR = 4,
    NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR = 5,
} nt_texture_default_filter_t;

typedef enum {
    NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE = 0,
    NT_TEXTURE_DEFAULT_WRAP_REPEAT = 1,
    NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT = 2,
} nt_texture_default_wrap_t;

/*
 * TextureAssetHeader -- 28 bytes (V3).
 *
 * Layout:
 *   magic(4) + version(2) + format(2) +
 *   width(4) + height(4) + mip_count(2) +
 *   compression(1) + flags(1) +
 *   default_min_filter(1) + default_mag_filter(1) +
 *   default_wrap_u(1) + default_wrap_v(1) +
 *   data_size(4)
 *
 * compression field:
 *   RAW(0): uncompressed pixel data follows, data_size = width*height*bpp
 *   BASIS(1): Basis Universal encoded blob follows
 *
 * format field serves double duty:
 *   RAW: pixel layout (RGBA8, RGB8, RG8, R8)
 *   BASIS: source channel config (RGBA8 = has alpha, RGB8 = no alpha)
 *
 * flags field:
 *   bit 0 = NT_TEXTURE_FLAG_PREMULTIPLIED — RGB values are already multiplied
 *           by alpha. Renderer should use blend (ONE, ONE_MINUS_SRC_ALPHA).
 *   bit 1 = NT_TEXTURE_FLAG_GEN_MIPMAPS — runtime should generate mips on
 *           upload for RAW textures so material sampler overrides can opt
 *           into mipmap filtering. No effect on BASIS (mips are baked).
 *
 * default_min/mag_filter, default_wrap_u/v:
 *   Builder-baked defaults (nt_texture_filter_t / nt_texture_wrap_t enum
 *   values from engine/graphics/nt_gfx.h). Activator creates a sampler
 *   object from these and binds it alongside the texture so the GPU
 *   samples with the asset's intended filter unless a material overrides.
 *   Pixel-art atlases typically pick LINEAR/NEAREST + REPEAT; downscaled
 *   3D textures want LINEAR_MIPMAP_LINEAR.
 *
 * Bump from V2 to V3: added 4 sampler-default bytes. Older V2 packs need
 * to be rebuilt — no compat shim, this is a feature-branch change.
 *
 * No mip_sizes[] array: RAW mips are calculable from dimensions,
 * BASIS mip boundaries are parsed internally by the transcoder.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;             /* NT_TEXTURE_MAGIC */
    uint16_t version;           /* NT_TEXTURE_VERSION (= 3) */
    uint16_t format;            /* nt_texture_pixel_format_t (source channel config for BASIS) */
    uint32_t width;             /* base level width in pixels */
    uint32_t height;            /* base level height in pixels */
    uint16_t mip_count;         /* number of mip levels in chain */
    uint8_t compression;        /* nt_texture_compression_t: 0=RAW, 1=BASIS */
    uint8_t flags;              /* NT_TEXTURE_FLAG_* bits */
    uint8_t default_min_filter; /* nt_texture_filter_t enum */
    uint8_t default_mag_filter; /* nt_texture_filter_t enum (NEAREST or LINEAR only) */
    uint8_t default_wrap_u;     /* nt_texture_wrap_t enum */
    uint8_t default_wrap_v;     /* nt_texture_wrap_t enum */
    uint32_t data_size;         /* total bytes of data after this header */
} NtTextureAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtTextureAssetHeader) == 28, "TextureAssetHeader must be 28 bytes");

/* Backward compat alias — code that used V2 suffix continues to compile.
 * The struct is the V3 layout; the alias is purely a name. */
typedef NtTextureAssetHeader NtTextureAssetHeaderV2;
#define NT_TEXTURE_VERSION_V2 NT_TEXTURE_VERSION

#endif /* NT_TEXTURE_FORMAT_H */
