#ifndef NT_SHADER_FORMAT_H
#define NT_SHADER_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "SHDR" as uint32_t little-endian = 0x52444853 */
#define NT_SHADER_MAGIC 0x52444853
#define NT_SHADER_VERSION 1

/*
 * ShaderAssetHeader -- binary header prepended to shader data in NEOPAK.
 *
 * Layout (24 bytes):
 *   magic(4) + version(2) + _pad(2) +
 *   vs_offset(4) + vs_size(4) +
 *   fs_offset(4) + fs_size(4)
 *
 * vs_offset and fs_offset are relative to the start of this header.
 * Shader source is null-terminated text. vs_size and fs_size include
 * the null terminator.
 *
 * Typical layout:
 *   [ShaderAssetHeader][vertex source bytes][fragment source bytes]
 *   vs_offset = sizeof(ShaderAssetHeader) = 24
 *   fs_offset = vs_offset + vs_size
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;     /* NT_SHADER_MAGIC */
    uint16_t version;   /* NT_SHADER_VERSION */
    uint16_t _pad;      /* explicit padding */
    uint32_t vs_offset; /* vertex shader source offset from header start */
    uint32_t vs_size;   /* vertex shader source size (incl. null terminator) */
    uint32_t fs_offset; /* fragment shader source offset from header start */
    uint32_t fs_size;   /* fragment shader source size (incl. null terminator) */
} NtShaderAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtShaderAssetHeader) == 24, "ShaderAssetHeader must be 24 bytes");

#endif /* NT_SHADER_FORMAT_H */
