#ifndef NT_TEST_PACK_H
#define NT_TEST_PACK_H

#include <stdint.h>
#include <string.h>

#include "nt_atlas_format.h"
#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "nt_font_format.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "nt_shader_format.h"
#include "nt_skeletal_format.h"
#include "nt_texture_format.h"

/* Fake payloads of at least this many bytes fit every asset header. */
#define NT_TEST_ASSET_PAYLOAD 64U

/* The resource core activates only payloads that start with their type's {magic, version}. */
static inline void nt_test_write_asset_header(uint8_t *payload, uint8_t asset_type) {
    uint32_t magic = 0;
    uint16_t version = 0;
    switch (asset_type) {
    case NT_ASSET_MESH:
        magic = NT_MESH_MAGIC;
        version = NT_MESH_VERSION;
        break;
    case NT_ASSET_TEXTURE:
        magic = NT_TEXTURE_MAGIC;
        version = NT_TEXTURE_VERSION;
        break;
    case NT_ASSET_SHADER_CODE:
        magic = NT_SHADER_CODE_MAGIC;
        version = NT_SHADER_CODE_VERSION;
        break;
    case NT_ASSET_BLOB:
        magic = NT_BLOB_MAGIC;
        version = NT_BLOB_VERSION;
        break;
    case NT_ASSET_FONT:
        magic = NT_FONT_MAGIC;
        version = NT_FONT_VERSION;
        break;
    case NT_ASSET_ATLAS:
        magic = NT_ATLAS_MAGIC;
        version = NT_ATLAS_VERSION;
        break;
    case NT_ASSET_SKELETON:
        magic = NT_SKL_MAGIC;
        version = NT_SKELETAL_FORMAT_VERSION;
        break;
    case NT_ASSET_SKIN_BINDING:
        magic = NT_SKN_MAGIC;
        version = NT_SKELETAL_FORMAT_VERSION;
        break;
    case NT_ASSET_CLIP:
        magic = NT_ANM_MAGIC;
        version = NT_SKELETAL_FORMAT_VERSION;
        break;
    default:
        return;
    }
    memcpy(payload, &magic, sizeof magic);
    memcpy(payload + sizeof magic, &version, sizeof version);
}

/* Writes every in-range entry's asset header, then the pack CRC. Call after the last entry edit. */
static inline void nt_test_seal_pack(uint8_t *blob) {
    NtPackHeader *h = (NtPackHeader *)blob;
    const NtAssetEntry *entries = (const NtAssetEntry *)(blob + sizeof(NtPackHeader));
    for (uint32_t i = 0; i < h->asset_count; i++) {
        if (entries[i].size >= sizeof(uint32_t) + sizeof(uint16_t) && entries[i].size <= h->total_size && entries[i].offset >= h->header_size && entries[i].offset <= h->total_size - entries[i].size) {
            nt_test_write_asset_header(blob + entries[i].offset, entries[i].asset_type);
        }
    }
    h->checksum = nt_crc32(blob + h->header_size, h->total_size - h->header_size);
}

#endif /* NT_TEST_PACK_H */
