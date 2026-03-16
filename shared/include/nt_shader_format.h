#ifndef NT_SHADER_FORMAT_H
#define NT_SHADER_FORMAT_H

#include <stdint.h>

/*
 * Shader code asset format for ntpack.
 *
 * Each shader source (vertex or fragment) is stored as a separate asset
 * (NT_ASSET_SHADER_CODE). This allows reusing one vertex shader with
 * multiple fragment shaders. Combining VS + FS into a GPU program is
 * a runtime/material concern, not a pack concern.
 */

/* Magic: ASCII "SHDC" as uint32_t little-endian = 0x43444853 */
#define NT_SHADER_CODE_MAGIC 0x43444853
#define NT_SHADER_CODE_VERSION 1

typedef enum {
    NT_SHADER_STAGE_VERTEX = 0,
    NT_SHADER_STAGE_FRAGMENT = 1,
} nt_shader_stage_t;

/*
 * NtShaderCodeHeader — one shader source in ntpack.
 *
 * Layout (12 bytes):
 *   magic(4) + version(2) + stage(1) + _pad(1) + code_size(4)
 *
 * After header: code_size bytes of null-terminated GLSL ES 3.00 source text.
 * WebGL2 compiles shaders at runtime (glCompileShader), no offline compilation.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;     /* 0:  NT_SHADER_CODE_MAGIC ("SHDC") */
    uint16_t version;   /* 4:  NT_SHADER_CODE_VERSION */
    uint8_t stage;      /* 6:  nt_shader_stage_t (vertex or fragment) */
    uint8_t _pad;       /* 7:  explicit padding */
    uint32_t code_size; /* 8:  source size in bytes (incl. null terminator) */
} NtShaderCodeHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtShaderCodeHeader) == 12, "ShaderCodeHeader must be 12 bytes");

#endif /* NT_SHADER_FORMAT_H */
