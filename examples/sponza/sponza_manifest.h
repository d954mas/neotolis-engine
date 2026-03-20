#ifndef SPONZA_MANIFEST_H
#define SPONZA_MANIFEST_H

#include <stdint.h>

/* Shader permutation types */
enum { SPONZA_SHADER_FULL = 0, SPONZA_SHADER_DIFFUSE = 1, SPONZA_SHADER_ALPHA = 2 };

/* Scene manifest binary format — packed into NEOPAK as a blob.
 * Builder writes, runtime reads. Both include this header. */

#pragma pack(push, 1)
typedef struct {
    uint32_t node_count;
    uint32_t _pad;
} SponzaManifestHeader;

typedef struct {
    uint64_t mesh_rid;
    uint64_t diffuse_rid;
    uint64_t normal_rid;
    uint64_t specular_rid;
    float transform[16];
    float base_color[4];
    uint8_t shader_type;       /* SPONZA_SHADER_* */
    uint8_t alpha_cutoff_x100; /* alpha cutoff * 100 */
    uint8_t _pad[6];
} SponzaManifestNode;
#pragma pack(pop)

_Static_assert(sizeof(SponzaManifestHeader) == 8, "SponzaManifestHeader must be 8 bytes");
_Static_assert(sizeof(SponzaManifestNode) == 120, "SponzaManifestNode must be 120 bytes");

#endif /* SPONZA_MANIFEST_H */
