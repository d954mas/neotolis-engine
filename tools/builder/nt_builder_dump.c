/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_crc32.h"
#include "log/nt_log.h"
/* clang-format on */

static void nt_format_size(uint32_t bytes, char *buf, size_t buf_size) {
    if (bytes >= 1024 * 1024) {
        (void)snprintf(buf, buf_size, "%.3f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        (void)snprintf(buf, buf_size, "%.3f KB", (double)bytes / 1024.0);
    } else {
        (void)snprintf(buf, buf_size, "%u B", bytes);
    }
}

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
        NT_LOG_ERROR("pack_path is NULL");
        return NT_BUILD_ERR_VALIDATION;
    }

    FILE *file = fopen(pack_path, "rb");
    if (!file) {
        NT_LOG_ERROR("Cannot open pack file: %s", pack_path);
        return NT_BUILD_ERR_IO;
    }

    /* Get file size */
    if (fseek(file, 0, SEEK_END) != 0) {
        NT_LOG_ERROR("Failed to seek in file");
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }
    long file_size_long = ftell(file);
    if (file_size_long < 0) {
        NT_LOG_ERROR("Failed to get file size");
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }
    (void)fseek(file, 0, SEEK_SET);

    uint32_t file_size = (uint32_t)file_size_long;

    /* Validate minimum size */
    if (file_size < sizeof(NtPackHeader)) {
        NT_LOG_ERROR("File too small (%u bytes, need at least %u)", file_size, (uint32_t)sizeof(NtPackHeader));
        (void)fclose(file);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Read entire file */
    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        NT_LOG_ERROR("Failed to allocate read buffer (%u bytes)", file_size);
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }

    if (fread(buffer, file_size, 1, file) != 1) {
        NT_LOG_ERROR("Failed to read file");
        free(buffer);
        (void)fclose(file);
        return NT_BUILD_ERR_IO;
    }
    (void)fclose(file);

    /* Parse header */
    const NtPackHeader *header = (const NtPackHeader *)buffer;

    /* Validate magic */
    if (header->magic != NT_PACK_MAGIC) {
        NT_LOG_ERROR("Invalid magic 0x%08X (expected 0x%08X)", header->magic, NT_PACK_MAGIC);
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Validate version */
    if (header->version > NT_PACK_VERSION_MAX) {
        NT_LOG_ERROR("Unsupported version %u (max %u)", header->version, NT_PACK_VERSION_MAX);
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Validate sizes */
    if (header->total_size != file_size) {
        NT_LOG_WARN("total_size mismatch (header: %u, file: %u)", header->total_size, file_size);
    }

    if (header->header_size > file_size) {
        NT_LOG_ERROR("header_size (%u) exceeds file size (%u)", header->header_size, file_size);
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }

    /* Verify CRC32 */
    uint32_t data_region_size = file_size - header->header_size;
    uint32_t computed_crc = nt_crc32(buffer + header->header_size, data_region_size);
    const char *crc_match = (computed_crc == header->checksum) ? "OK" : "MISMATCH";

    /* Print pack summary */
    NT_LOG_INFO("Pack: %s", pack_path);
    NT_LOG_INFO("Version: %u", header->version);
    NT_LOG_INFO("Assets: %u", header->asset_count);
    char size_buf[32];
    NT_LOG_INFO("Header size: %u bytes", header->header_size);
    nt_format_size(header->total_size, size_buf, sizeof(size_buf));
    NT_LOG_INFO("Total size: %s (%u bytes)", size_buf, header->total_size);
    NT_LOG_INFO("CRC32: 0x%08X (%s)", header->checksum, crc_match);

    /* Parse and print entries */
    if (header->header_size < (uint32_t)sizeof(NtPackHeader)) {
        NT_LOG_ERROR("header_size (%u) smaller than PackHeader (%u)", header->header_size, (uint32_t)sizeof(NtPackHeader));
        free(buffer);
        return NT_BUILD_ERR_FORMAT;
    }
    const NtAssetEntry *entries = (const NtAssetEntry *)(buffer + sizeof(NtPackHeader));
    uint32_t max_entries = (header->header_size - (uint32_t)sizeof(NtPackHeader)) / (uint32_t)sizeof(NtAssetEntry);
    uint32_t count = header->asset_count;
    if (count > max_entries) {
        NT_LOG_WARN("asset_count (%u) exceeds space for entries (%u)", count, max_entries);
        count = max_entries;
    }

    NT_LOG_INFO("  %-5s %-20s %-10s %-7s %-10s %-10s", "Index", "Resource ID", "Type", "FmtVer", "Offset", "Size");
    NT_LOG_INFO("  %-5s %-20s %-10s %-7s %-10s %-10s", "-----", "------------------", "--------", "------", "--------", "--------");

    for (uint32_t i = 0; i < count; i++) {
        nt_format_size(entries[i].size, size_buf, sizeof(size_buf));
        NT_LOG_INFO("  %-5u 0x%016llX   %-10s %-7u %-10u %-10s", i, (unsigned long long)entries[i].resource_id, nt_asset_type_name(entries[i].asset_type), entries[i].format_version, entries[i].offset,
                    size_buf);
    }

    free(buffer);
    return NT_BUILD_OK;
}
