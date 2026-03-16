#ifndef NT_MESH_FORMAT_H
#define NT_MESH_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "MESH" as uint32_t little-endian = 0x4853454D */
#define NT_MESH_MAGIC 0x4853454D
#define NT_MESH_VERSION 1

/* Attribute mask bits -- positions MUST match NT_ATTR_* in nt_gfx.h */
#define NT_MESH_ATTR_POSITION (1u << 0)  /* NT_ATTR_POSITION = 0 */
#define NT_MESH_ATTR_NORMAL (1u << 1)    /* NT_ATTR_NORMAL = 1 */
#define NT_MESH_ATTR_COLOR (1u << 2)     /* NT_ATTR_COLOR = 2 */
#define NT_MESH_ATTR_TEXCOORD0 (1u << 3) /* NT_ATTR_TEXCOORD0 = 3 */

/* Required attributes for a valid mesh */
#define NT_MESH_ATTR_REQUIRED NT_MESH_ATTR_POSITION

/* stub -- will be replaced */
typedef struct {
    uint32_t magic;
} NtMeshAssetHeader;

_Static_assert(sizeof(NtMeshAssetHeader) == 24, "MeshAssetHeader must be 24 bytes");

#endif /* NT_MESH_FORMAT_H */
