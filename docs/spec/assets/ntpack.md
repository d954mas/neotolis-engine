# Pack Format (NTPACK)

NTPACK is a custom flat binary pack format: 32-byte header with embedded
manifest, packed asset entries, aligned asset data, and an optional metadata
section. No backwards compatibility — the runtime asserts the exact version and
old packs are rebuilt. Parsing is direct struct reads; asset access is zero-copy.

Related: [Resource System](resource.md), [Async Loading](async-loading.md), [Builder Architecture](../builder/builder.md), [Runtime Formats](runtime-formats.md)

## Design rationale

Custom flat binary format instead of ZIP. Rationale:

- no external library dependency (no miniz in WASM, saves ~15-25KB binary size)
- trivial parsing: direct struct reads, no variable-length header parsing
- zero-copy asset access: pointer + offset into loaded blob
- manifest is embedded in header, not a separate file
- HTTP transport compression (gzip/brotli) handles delivery size
- partial loading via HTTP Range requests is straightforward (header first, then assets by offset)

## Binary layout

```text
┌──────────────────────────────────────┐
│ NtPackHeader (32 bytes, packed)       │
│   magic: uint32     "NPAK"           │
│   meta_count: uint32                 │
│   version: uint16   NT_PACK_VERSION  │
│   asset_count: uint16                │
│   header_size: uint32  ← data start  │
│   total_size: uint32                  │
│   checksum: uint32     ← CRC32       │
│   meta_offset: uint32  ← meta start  │
│   _pad: uint32      (8-byte align)   │
├──────────────────────────────────────┤
│ NtAssetEntry[0] (24 bytes, packed)    │
│   resource_id: uint64                 │
│   offset: uint32  ← from file start  │
│   size: uint32                        │
│   format_version: uint16              │
│   asset_type: uint8                   │
│   _pad: uint8                         │
│   meta_offset: uint32  ← per-asset   │
├──────────────────────────────────────┤
│ NtAssetEntry[1..N-1]                  │
│   ...                                 │
╞══════════════════════════════════════╡
│ [padding to 8-byte alignment]         │
│ [asset 0 binary data]                 │
│ [asset 1 binary data]                 │
│ ...                                   │
│ [asset N-1 binary data]               │
╞══════════════════════════════════════╡
│ [meta section] (optional)             │
│   NtMetaEntryHeader + payload ...     │
│   grouped by resource_id              │
└──────────────────────────────────────┘
```

Assets aligned to 4 bytes (NT_PACK_ASSET_ALIGN). Header/entries region aligned to 8 bytes (NT_PACK_DATA_ALIGN) before data start. Meta section appended after asset data, covered by CRC32. Resident copy made at parse time (survives blob eviction).

## Version policy

No backwards compatibility. Runtime asserts `version == NT_PACK_VERSION`. Old packs must be rebuilt when format changes. This is intentional: the engine is in active development, and maintaining backwards compat for a format that changes frequently adds complexity without benefit. Builder and runtime always agree on version.

## Metadata section

Optional section after asset data. Contains variable-length entries (NtMetaEntryHeader + payload) grouped by resource_id. Header-level `meta_offset` points to section start; per-asset `meta_offset` points to first entry for that asset. Used for game-defined metadata (tags, material bindings, custom properties). AABB is not metadata — it lives in NtMeshAssetHeader as inherent mesh data.

```c
NtMetaEntryHeader (20 bytes, packed):
    uint64_t resource_id;  /* which asset */
    uint64_t kind;         /* hash64 of metadata type name */
    uint32_t size;         /* payload bytes (max 256) */
    /* uint8_t data[size] follows immediately */
```

Query: `nt_resource_get_meta(handle, nt_hash64_str("tag").value, &size)` — returns pointer to resident memory, NULL if absent.

## Runtime parsing

```c
// Pseudocode — see nt_resource.c for actual implementation
void parse_pack(const uint8_t *blob, uint32_t blob_size) {
    const NtPackHeader *h = (const NtPackHeader *)blob;

    NT_ASSERT(h->magic == NT_PACK_MAGIC);
    NT_ASSERT(h->version == NT_PACK_VERSION); /* no backwards compat */

    const NtAssetEntry *entries = (const NtAssetEntry *)(blob + sizeof(NtPackHeader));

    for (uint16_t i = 0; i < h->asset_count; i++) {
        NtAssetMeta *meta = asset_alloc();
        meta->resource_id = entries[i].resource_id;
        meta->offset = entries[i].offset;
        meta->size = entries[i].size;
        /* Convert per-asset meta_offset from absolute to meta_data-relative */
        meta->meta_offset = (entries[i].meta_offset != 0) ? entries[i].meta_offset - h->meta_offset : NT_NO_METADATA;
    }

    /* Copy meta section to resident memory (survives blob eviction) */
    if (h->meta_count > 0 && h->meta_offset != 0) {
        uint32_t meta_size = blob_size - h->meta_offset;
        pack->meta_data = malloc(meta_size);
        memcpy(pack->meta_data, blob + h->meta_offset, meta_size);
    }
}
```

## Asset data access

```c
const uint8_t *pack_get_asset_data(const PackMeta *pack, uint32_t offset, uint32_t size) {
    return pack->blob_data + offset;
}
```

Zero copy. Data is already in WASM heap.

## Debugging

Builder includes the `nt_builder_dump_pack(pack_path)` utility that prints pack contents to console. No external tool needed.

## Future: partial loading

Flat layout allows HTTP Range requests: load first `header_size` bytes to get manifest, then load individual assets by offset/size on demand.
