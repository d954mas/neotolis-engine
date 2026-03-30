/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_crc32.h"
#include "nt_font_format.h"
#include "nt_texture_format.h"
#include "miniz.h"
/* clang-format on */

/* ---- Name resolution from .h file ---- */

#define MAX_NAME_ENTRIES 1024

typedef struct {
    uint64_t hash;
    char name[256];
} NameEntry;

static uint32_t parse_header_file(const char *header_path, NameEntry *entries, uint32_t max_entries) {
    FILE *f = fopen(header_path, "r");
    if (!f) {
        return 0;
    }
    char line[512];
    uint32_t count = 0;
    while (fgets(line, (int)sizeof(line), f) && count < max_entries) {
        char path_buf[256];
        /* Find the hex value between {0x and ULL} */
        const char *hex_start = strstr(line, "{0x");
        const char *ull_end = strstr(line, "ULL}");
        if (!hex_start || !ull_end || ull_end <= hex_start) {
            continue;
        }
        /* Parse hex value using strtoull (cert-err34-c compliant) */
        char *endptr = NULL;
        unsigned long long hash_val = strtoull(hex_start + 1, &endptr, 16); /* skip '{', parse "0x..." */
        if (!endptr || endptr == hex_start + 1) {
            continue;
        }
        /* Extract path from comment: / * path * / */
        if (sscanf(line, "#define %*s %*s /* %255[^*]", path_buf) != 1) {
            continue;
        }
        /* Trim trailing whitespace from path_buf */
        size_t len = strlen(path_buf);
        while (len > 0 && (path_buf[len - 1] == ' ' || path_buf[len - 1] == '\n' || path_buf[len - 1] == '\r')) {
            path_buf[--len] = '\0';
        }
        entries[count].hash = (uint64_t)hash_val;
        (void)strncpy(entries[count].name, path_buf, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        count++;
    }
    (void)fclose(f);
    return count;
}

static const char *lookup_name(uint64_t hash, const NameEntry *entries, uint32_t entry_count, char *fallback_buf, size_t fallback_buf_size) {
    for (uint32_t i = 0; i < entry_count; i++) {
        if (entries[i].hash == hash) {
            return entries[i].name;
        }
    }
    /* Fallback: truncated hex hash (last 12 chars) */
    (void)snprintf(fallback_buf, fallback_buf_size, "...%012llX", (unsigned long long)hash);
    return fallback_buf;
}

/* ---- Gzip size estimation via miniz ---- */

static uint32_t estimate_gzip_size(const uint8_t *data, uint32_t size, uint8_t *compress_buf, mz_ulong compress_buf_size) {
    if (size == 0) {
        return 0;
    }
    mz_ulong compressed_size = compress_buf_size;
    int status = mz_compress2(compress_buf, &compressed_size, data, (mz_ulong)size, MZ_DEFAULT_LEVEL);
    if (status != MZ_OK) {
        return size; /* fallback: report raw size if compression fails */
    }
    return (uint32_t)compressed_size;
}

/* ---- Texture format detection ---- */

/*
 * Basis Universal file header layout (from basisu_file_headers.h):
 *
 *   offset  0: m_sig          (2 bytes)  signature 0x4273 ('sB')
 *   offset  2: m_ver          (2 bytes)  file version
 *   offset  4: m_header_size  (2 bytes)
 *   offset  6: m_header_crc16 (2 bytes)
 *   offset  8: m_data_size    (4 bytes)
 *   offset 12: m_data_crc16   (2 bytes)
 *   offset 14: m_total_slices (3 bytes)
 *   offset 17: m_total_images (3 bytes)
 *   offset 20: m_tex_format   (1 byte)   basis_tex_format enum: 0=ETC1S, 1=UASTC
 *   offset 21: m_flags        (2 bytes)
 *   ...
 *
 * We read m_tex_format directly — more reliable than checking header flags.
 */
#define BASIS_SIG_OFFSET 0
#define BASIS_SIG_VALUE 0x4273 /* 'sB' little-endian */
#define BASIS_TEX_FORMAT_OFFSET 20
#define BASIS_MIN_HEADER_SIZE 23 /* enough to read through m_tex_format */
#define BASIS_TEX_FORMAT_ETC1S 0
#define BASIS_TEX_FORMAT_UASTC 1

/* Returns 0=ETC1S, 1=UASTC, -1=unknown/too small/bad signature */
static int basis_detect_mode(const uint8_t *basis_data, uint32_t basis_size) {
    if (basis_size < BASIS_MIN_HEADER_SIZE) {
        return -1;
    }
    uint16_t sig = (uint16_t)(basis_data[BASIS_SIG_OFFSET] | ((uint16_t)basis_data[BASIS_SIG_OFFSET + 1] << 8));
    if (sig != BASIS_SIG_VALUE) {
        return -1;
    }
    return (int)basis_data[BASIS_TEX_FORMAT_OFFSET];
}

static const char *texture_format_str(const uint8_t *asset_data, uint32_t asset_size) {
    if (!asset_data || asset_size < sizeof(NtTextureAssetHeader)) {
        return "TEX";
    }
    const NtTextureAssetHeader *hdr = (const NtTextureAssetHeader *)asset_data;
    if (hdr->compression == NT_TEXTURE_COMPRESSION_RAW) {
        return "TEX|RAW";
    }
    if (hdr->compression == NT_TEXTURE_COMPRESSION_BASIS) {
        const uint8_t *basis_data = asset_data + sizeof(NtTextureAssetHeader);
        uint32_t basis_size = asset_size - (uint32_t)sizeof(NtTextureAssetHeader);
        int mode = basis_detect_mode(basis_data, basis_size);
        if (mode == BASIS_TEX_FORMAT_ETC1S) {
            return "TEX|ETC1S";
        }
        if (mode == BASIS_TEX_FORMAT_UASTC) {
            return "TEX|UASTC";
        }
        return "TEX|BASIS";
    }
    return "TEX";
}

/* ---- Truncate long names (keep tail, prefix with "...") ---- */

#define DUMP_NAME_WIDTH 40

static const char *truncate_name(const char *name, char *buf, size_t buf_size) {
    size_t len = strlen(name);
    if (len <= DUMP_NAME_WIDTH) {
        return name;
    }
    /* Show "..." + last (DUMP_NAME_WIDTH - 3) chars */
    size_t tail = DUMP_NAME_WIDTH - 3;
    (void)snprintf(buf, buf_size, "...%s", name + len - tail);
    return buf;
}

/* ---- Asset type name ---- */

static const char *nt_asset_type_name(uint8_t type) {
    switch (type) {
    case NT_ASSET_MESH:
        return "MESH";
    case NT_ASSET_TEXTURE:
        return "TEXTURE";
    case NT_ASSET_SHADER_CODE:
        return "SHADER";
    case NT_ASSET_BLOB:
        return "BLOB";
    case NT_ASSET_FONT:
        return "FONT";
    default:
        return "UNKNOWN";
    }
}

/* Short type tag for the table (merges type + texture format) */
static const char *asset_type_tag(uint8_t type, const uint8_t *asset_data, uint32_t asset_size) {
    if (type == NT_ASSET_TEXTURE) {
        return texture_format_str(asset_data, asset_size);
    }
    return nt_asset_type_name(type);
}

/* ---- Duplicate detection ---- */

static int32_t find_duplicate_original(const NtAssetEntry *entries, uint32_t current_index) {
    for (uint32_t i = 0; i < current_index; i++) {
        if (entries[i].offset == entries[current_index].offset && entries[i].size == entries[current_index].size) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ---- Header path derivation (uses shared utility from nt_builder_internal.h) ---- */

/* ---- Font-specific detail printer ---- */

static void print_font_details(const uint8_t *asset_data, uint32_t asset_size) {
    if (!asset_data || asset_size < sizeof(NtFontAssetHeader)) {
        return;
    }
    const NtFontAssetHeader *fhdr = (const NtFontAssetHeader *)asset_data;
    if (fhdr->magic != NT_FONT_MAGIC) {
        return;
    }

    NT_LOG_INFO("         glyphs: %u  units_per_em: %u  ascent: %d  descent: %d", fhdr->glyph_count, fhdr->units_per_em, fhdr->ascent, fhdr->descent);

    /* Print character list: show all included codepoints as readable text */
    if (fhdr->glyph_count == 0) {
        return;
    }
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(asset_data + sizeof(NtFontAssetHeader));
    uint32_t max_glyphs = (asset_size - (uint32_t)sizeof(NtFontAssetHeader)) / (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t count = fhdr->glyph_count;
    if (count > max_glyphs) {
        count = max_glyphs;
    }

    // #region Build character list string -- encode codepoints as UTF-8
    char chars[1024];
    uint32_t pos = 0;
    for (uint32_t g = 0; g < count && pos < sizeof(chars) - 5; g++) {
        uint32_t cp = glyphs[g].codepoint;
        if (cp < 0x20) {
            continue; /* skip control characters */
        }
        if (cp < 0x80) {
            chars[pos++] = (char)cp;
        } else if (cp < 0x800) {
            chars[pos++] = (char)(0xC0 | (cp >> 6));
            chars[pos++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            chars[pos++] = (char)(0xE0 | (cp >> 12));
            chars[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            chars[pos++] = (char)(0x80 | (cp & 0x3F));
        } else {
            chars[pos++] = (char)(0xF0 | (cp >> 18));
            chars[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            chars[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            chars[pos++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    chars[pos] = '\0';
    // #endregion
    NT_LOG_INFO("         chars: %s", chars);
}

/* ---- Per-type summary accumulators ---- */

typedef struct {
    uint32_t mesh_count;
    uint32_t mesh_raw;
    uint32_t tex_count;
    uint32_t tex_raw;
    uint32_t tex_raw_fmt;
    uint32_t tex_etc1s;
    uint32_t tex_uastc;
    uint32_t shader_count;
    uint32_t shader_raw;
    uint32_t blob_count;
    uint32_t blob_raw;
    uint32_t font_count;
    uint32_t font_raw;
    uint32_t total_raw;
    uint32_t total_gz;
    uint32_t dup_count;
    uint32_t dup_saved;
} DumpStats;

static void accumulate_texture_stats(DumpStats *st, const uint8_t *asset_data, uint32_t asset_size) {
    if (!asset_data || asset_size < sizeof(NtTextureAssetHeader)) {
        return;
    }
    const NtTextureAssetHeader *thdr = (const NtTextureAssetHeader *)asset_data;
    if (thdr->compression == NT_TEXTURE_COMPRESSION_RAW) {
        st->tex_raw_fmt++;
    } else if (thdr->compression == NT_TEXTURE_COMPRESSION_BASIS) {
        const uint8_t *basis_data = asset_data + sizeof(NtTextureAssetHeader);
        uint32_t basis_size = asset_size - (uint32_t)sizeof(NtTextureAssetHeader);
        int mode = basis_detect_mode(basis_data, basis_size);
        if (mode == BASIS_TEX_FORMAT_ETC1S) {
            st->tex_etc1s++;
        } else if (mode == BASIS_TEX_FORMAT_UASTC) {
            st->tex_uastc++;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void accumulate_stats(DumpStats *st, const NtAssetEntry *e, const uint8_t *asset_data, uint32_t gz_size, int32_t dup_of) {
    uint32_t asset_size = e->size;
    st->total_raw += asset_size;
    st->total_gz += gz_size;

    if (dup_of >= 0) {
        st->dup_count++;
        st->dup_saved += asset_size;
    }

    switch (e->asset_type) {
    case NT_ASSET_MESH:
        st->mesh_count++;
        st->mesh_raw += asset_size;
        break;
    case NT_ASSET_TEXTURE:
        st->tex_count++;
        st->tex_raw += asset_size;
        accumulate_texture_stats(st, asset_data, asset_size);
        break;
    case NT_ASSET_SHADER_CODE:
        st->shader_count++;
        st->shader_raw += asset_size;
        break;
    case NT_ASSET_BLOB:
        st->blob_count++;
        st->blob_raw += asset_size;
        break;
    case NT_ASSET_FONT:
        st->font_count++;
        st->font_raw += asset_size;
        break;
    default:
        break;
    }
}

static void print_type_line(const char *label, uint32_t count, uint32_t raw_bytes) {
    char sz[16];
    nt_format_size(raw_bytes, sz, sizeof(sz));
    NT_LOG_INFO("  %-8s %u asset%s, %s raw", label, count, count > 1 ? "s" : "", sz);
}

static void print_texture_summary(const DumpStats *st) {
    char sz[16];
    nt_format_size(st->tex_raw, sz, sizeof(sz));
    /* Build format breakdown string */
    char fmt_str[64] = "";
    size_t pos = 0;
    uint32_t parts = 0;
    if (st->tex_raw_fmt > 0) {
        pos += (size_t)snprintf(fmt_str + pos, sizeof(fmt_str) - pos, "%u RAW", st->tex_raw_fmt);
        parts++;
    }
    if (st->tex_etc1s > 0) {
        pos += (size_t)snprintf(fmt_str + pos, sizeof(fmt_str) - pos, "%s%u ETC1S", parts > 0 ? ", " : "", st->tex_etc1s);
        parts++;
    }
    if (st->tex_uastc > 0) {
        pos += (size_t)snprintf(fmt_str + pos, sizeof(fmt_str) - pos, "%s%u UASTC", parts > 0 ? ", " : "", st->tex_uastc);
        parts++;
    }
    (void)pos;
    if (parts > 0) {
        NT_LOG_INFO("  %-8s %u asset%s, %s raw (%s)", "TEX:", st->tex_count, st->tex_count > 1 ? "s" : "", sz, fmt_str);
    } else {
        NT_LOG_INFO("  %-8s %u asset%s, %s raw", "TEX:", st->tex_count, st->tex_count > 1 ? "s" : "", sz);
    }
}

static void print_summary(const DumpStats *st) {
    NT_LOG_INFO("");
    NT_LOG_INFO("Summary:");

    if (st->mesh_count > 0) {
        print_type_line("MESH:", st->mesh_count, st->mesh_raw);
    }
    if (st->tex_count > 0) {
        print_texture_summary(st);
    }
    if (st->shader_count > 0) {
        print_type_line("SHADER:", st->shader_count, st->shader_raw);
    }
    if (st->blob_count > 0) {
        print_type_line("BLOB:", st->blob_count, st->blob_raw);
    }
    if (st->font_count > 0) {
        print_type_line("FONT:", st->font_count, st->font_raw);
    }
    if (st->dup_count > 0) {
        char sz[16];
        nt_format_size(st->dup_saved, sz, sizeof(sz));
        NT_LOG_INFO("  Duplicates: %u (%s saved)", st->dup_count, sz);
    }

    /* Total with gzip ratio */
    char raw_str[16];
    char gz_str[16];
    nt_format_size(st->total_raw, raw_str, sizeof(raw_str));
    nt_format_size(st->total_gz, gz_str, sizeof(gz_str));
    uint32_t pct = (st->total_raw > 0) ? ((st->total_gz * 100) / st->total_raw) : 100;
    NT_LOG_INFO("  Total:   %s raw -> %s gz (%u%%)", raw_str, gz_str, pct);
}

/* ---- Main dump function ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

    /* Validate version -- no backwards compat */
    if (header->version != NT_PACK_VERSION) {
        NT_LOG_ERROR("Version mismatch %u (expected %u) -- rebuild packs", header->version, NT_PACK_VERSION);
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
    const char *crc_status = (computed_crc == header->checksum) ? "OK" : "MISMATCH";

    /* Parse entries */
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

    /* Parse .h file for name resolution */
    NameEntry *name_entries = (NameEntry *)calloc(MAX_NAME_ENTRIES, sizeof(NameEntry));
    uint32_t name_count = 0;
    if (name_entries) {
        char hdr_path[512];
        nt_builder_pack_to_header_path(pack_path, hdr_path, sizeof(hdr_path));
        name_count = parse_header_file(hdr_path, name_entries, MAX_NAME_ENTRIES);
    }

    /* Allocate compression buffer (reuse for all assets) */
    uint32_t max_asset_size = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].size > max_asset_size) {
            max_asset_size = entries[i].size;
        }
    }
    mz_ulong compress_bound = mz_compressBound((mz_ulong)max_asset_size);
    uint8_t *compress_buf = (uint8_t *)malloc((size_t)compress_bound);

    /* Print pack header */
    NT_LOG_INFO("Pack: %s (v%u, %u assets)", pack_path, header->version, header->asset_count);
    NT_LOG_INFO("CRC32: 0x%08X %s", header->checksum, crc_status);
    NT_LOG_INFO("");
    NT_LOG_INFO("  %-3s %-40s %-10s %-28s %s", "#", "Name", "Type", "Size", "Note");
    NT_LOG_INFO("  %-3s %-40s %-10s %-28s %s", "-", "----", "----", "----", "----");

    /* Per-type accumulators */
    DumpStats stats;
    memset(&stats, 0, sizeof(stats));

    /* Print each entry */
    for (uint32_t i = 0; i < count; i++) {
        const NtAssetEntry *e = &entries[i];
        bool in_bounds = (e->offset < file_size) && (e->size <= file_size - e->offset);
        const uint8_t *asset_data = in_bounds ? (buffer + e->offset) : NULL;
        uint32_t asset_size = in_bounds ? e->size : 0;

        /* Name resolution */
        char fallback_name[32];
        const char *name = lookup_name(e->resource_id, name_entries, name_count, fallback_name, sizeof(fallback_name));

        /* Type tag (handles texture sub-formats) */
        const char *type_tag = asset_type_tag(e->asset_type, asset_data, asset_size);

        /* Gzip estimation */
        uint32_t gz_size = asset_size;
        if (compress_buf && asset_data && asset_size > 0) {
            gz_size = estimate_gzip_size(asset_data, asset_size, compress_buf, compress_bound);
        }

        /* Percentage */
        uint32_t pct = (asset_size > 0) ? ((gz_size * 100) / asset_size) : 100;

        /* Size string: "2.4K (1.8K gz 75%)" */
        char raw_str[16];
        char gz_str[16];
        char size_str[64];
        nt_format_size(asset_size, raw_str, sizeof(raw_str));
        nt_format_size(gz_size, gz_str, sizeof(gz_str));
        (void)snprintf(size_str, sizeof(size_str), "%s (%s gz %u%%)", raw_str, gz_str, pct);

        /* Duplicate detection */
        char note_str[32] = "";
        int32_t dup_of = find_duplicate_original(entries, i);
        if (dup_of >= 0) {
            (void)snprintf(note_str, sizeof(note_str), "dup #%d", dup_of);
        }

        char trunc_buf[DUMP_NAME_WIDTH + 1];
        const char *display_name = truncate_name(name, trunc_buf, sizeof(trunc_buf));
        NT_LOG_INFO("  %-3u %-40s %-10s %-28s %s", i, display_name, type_tag, size_str, note_str);

        /* Font-specific detail line */
        if (e->asset_type == NT_ASSET_FONT && asset_data) {
            print_font_details(asset_data, asset_size);
        }

        /* Accumulate per-type stats */
        accumulate_stats(&stats, e, asset_data, gz_size, dup_of);
    }

    print_summary(&stats);

    free(compress_buf);
    free(name_entries);
    free(buffer);
    return NT_BUILD_OK;
}
