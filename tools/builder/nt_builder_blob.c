/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_blob_format.h"
/* clang-format on */

/* --- Blob import (called from finish_pack) --- */

nt_build_result_t nt_builder_import_blob(NtBuilderContext *ctx, const void *data, uint32_t size, uint64_t resource_id) {
    if (!ctx || !data || size == 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    NtBlobAssetHeader blob_hdr;
    memset(&blob_hdr, 0, sizeof(blob_hdr));
    blob_hdr.magic = NT_BLOB_MAGIC;
    blob_hdr.version = NT_BLOB_VERSION;
    blob_hdr._pad = 0;

    uint32_t total_asset_size = (uint32_t)sizeof(NtBlobAssetHeader) + size;

    nt_build_result_t ret = nt_builder_append_data(ctx, &blob_hdr, (uint32_t)sizeof(NtBlobAssetHeader));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, data, size);
    }
    if (ret != NT_BUILD_OK) {
        return ret;
    }

    return nt_builder_register_asset(ctx, resource_id, NT_ASSET_BLOB, NT_BLOB_VERSION, total_asset_size);
}
