/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_crc32.h"
/* clang-format on */

static const char *nt_asset_type_name(uint8_t type) {
    switch (type) {
    case NT_ASSET_MESH:
        return "MESH";
    case NT_ASSET_TEXTURE:
        return "TEXTURE";
    case NT_ASSET_SHADER_CODE:
        return "SHADER";
    default:
        return "UNKNOWN";
    }
}

nt_build_result_t nt_builder_dump_pack(const char *pack_path) {
    if (!pack_path) {
        (void)fprintf(stderr, "ERROR: pack_path is NULL\n");
        return NT_BUILD_ERR_VALIDATION;
    }

    FILE *file = fopen(pack_path, "rb");
    if (!file) {
        (void)fprintf(stderr, "ERROR: Cannot open pack file: %s\n", pack_path);
        return NT_BUILD_ERR_IO;
    }

    /* Get file size */
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fprintf(stderr, "ERROR: Failed to seek in file\n");
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }
    long file_size_long = ftell(file);
    if (file_size_long < 0) {
        (void)fprintf(stderr, "ERROR: Failed to get file size\n");
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }
    rewind(file);

    uint32_t file_size = (uint32_t)file_size_long;

    /* Validate minimum size */
    if (file_size < sizeof(NtPackHeader)) {
        (void)fprintf(stderr, "ERROR: File too small (%u bytes, need at least %u)\n", file_size, (uint32_t)sizeof(NtPackHeader));
        (void)fclose(file);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Read entire file */
    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        (void)fprintf(stderr, "ERROR: Failed to allocate read buffer (%u bytes)\n", file_size);
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }

    if (fread(buffer, file_size, 1, file) != 1) {
        (void)fprintf(stderr, "ERROR: Failed to read file\n");
        free(buffer);
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }
    (void)fclose(file);

    /* Parse header */
    const NtPackHeader *header = (const NtPackHeader *)buffer;

    /* Validate magic */
    if (header->magic != NT_PACK_MAGIC) {
        (void)fprintf(stderr, "ERROR: Invalid magic 0x%08X (expected 0x%08X)\n", header->magic, NT_PACK_MAGIC);
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Validate version */
    if (header->version > NT_PACK_VERSION_MAX) {
        (void)fprintf(stderr, "ERROR: Unsupported version %u (max %u)\n", header->version, NT_PACK_VERSION_MAX);
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Validate sizes */
    if (header->total_size != file_size) {
        (void)fprintf(stderr, "WARNING: total_size mismatch (header: %u, file: %u)\n", header->total_size, file_size);
    }

    if (header->header_size > file_size) {
        (void)fprintf(stderr, "ERROR: header_size (%u) exceeds file size (%u)\n", header->header_size, file_size);
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Verify CRC32 */
    uint32_t data_region_size = file_size - header->header_size;
    uint32_t computed_crc = nt_crc32(buffer + header->header_size, data_region_size);
    const char *crc_match = (computed_crc == header->checksum) ? "OK" : "MISMATCH";

    /* Print pack summary */
    (void)printf("Pack: %s\n", pack_path);
    (void)printf("Pack ID: 0x%08X\n", header->pack_id);
    (void)printf("Version: %u\n", header->version);
    (void)printf("Assets: %u\n", header->asset_count);
    (void)printf("Header size: %u bytes\n", header->header_size);
    (void)printf("Total size: %u bytes\n", header->total_size);
    (void)printf("CRC32: 0x%08X (%s)\n", header->checksum, crc_match);
    (void)printf("\n");

    /* Parse and print entries */
    const NtAssetEntry *entries = (const NtAssetEntry *)(buffer + sizeof(NtPackHeader));
    uint32_t max_entries = (header->header_size - (uint32_t)sizeof(NtPackHeader)) / (uint32_t)sizeof(NtAssetEntry);
    uint32_t count = header->asset_count;
    if (count > max_entries) {
        (void)fprintf(stderr, "WARNING: asset_count (%u) exceeds space for entries (%u)\n", count, max_entries);
        count = max_entries;
    }

    (void)printf("  %-5s %-12s %-10s %-7s %-10s %-10s\n", "Index", "Resource ID", "Type", "FmtVer", "Offset", "Size");
    (void)printf("  %-5s %-12s %-10s %-7s %-10s %-10s\n", "-----", "----------", "--------", "------", "--------", "--------");

    for (uint32_t i = 0; i < count; i++) {
        (void)printf("  %-5u 0x%08X   %-10s %-7u %-10u %-10u\n", i, entries[i].resource_id, nt_asset_type_name(entries[i].asset_type), entries[i].format_version, entries[i].offset, entries[i].size);
    }

    free(buffer);
    return NT_BUILD_OK;
}
