#ifndef NT_SHADER_FORMAT_H
#define NT_SHADER_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "SHDR" as uint32_t little-endian = 0x52444853 */
#define NT_SHADER_MAGIC 0x52444853
#define NT_SHADER_VERSION 1

/* stub -- will be replaced */
typedef struct {
    uint32_t magic;
} NtShaderAssetHeader;

_Static_assert(sizeof(NtShaderAssetHeader) == 24,
               "ShaderAssetHeader must be 24 bytes");

#endif /* NT_SHADER_FORMAT_H */
