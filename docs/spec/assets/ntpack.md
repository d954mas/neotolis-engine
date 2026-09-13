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
│   owner_entry: uint16                 │
│   asset_type: uint8                   │
│   _pad: uint8                         │
│   meta_offset: uint32  ← per-asset   │
├──────────────────────────────────────┤
│ NtAssetEntry[1..N-1]                  │
│   ...                                 │
╞══════════════════════════════════════╡
│ [padding to 8-byte alignment]         │
│ [unique asset payloads + alignment]   │
│ aliases share their owner's range    │
╞══════════════════════════════════════╡
│ [meta section] (optional)             │
│   NtMetaEntryHeader + payload ...     │
│   grouped by resource_id              │
└──────────────────────────────────────┘
```

Assets aligned to 4 bytes (NT_PACK_ASSET_ALIGN). Header/entries region aligned to 8 bytes (NT_PACK_DATA_ALIGN) before data start. Meta section appended after asset data, covered by CRC32. Resident copy made at parse time (survives blob eviction).

Every entry stores its own file-relative `offset` and `size`, including aliases.
The builder adds the final `header_size` to payload-buffer offsets when writing
the manifest. `owner_entry` identifies shared runtime ownership; it is not needed
to find the payload bytes.

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

Query: `nt_resource_get_meta(handle, nt_hash64_str("tag"), &size)` — returns pointer to resident memory, NULL if absent.

## Runtime parsing

All asset byte ranges and ownership links are checked before any slots are
reserved or records modified. Parsing requires a file mount; virtual packs
accept registrations only. Invalid ranges, zero resource IDs or malformed
owner links reject the pack through the existing recoverable parse error; a
corrected pack can load into the same mount. Manifest types outside the defined
`NT_ASSET_MESH..NT_ASSET_ATLAS` range also reject the pack. Every non-BLOB file
type requires an activator configured before the first mount; missing one asserts
before reserving records or retaining bytes. Capacity exhaustion asserts.

Each mount accepts one successful parse, including an empty pack. A later parse
returns `NT_ERR_INVALID_ARG` even after blob eviction: eviction preserves the
registered assets, metadata and original blob size. Resource-managed I/O restores
evicted bytes without parsing the manifest again. The URL/path must return
identical bytes for the mount's lifetime: reload asserts the size matches but
does not recheck the CRC or manifest. Unmount and mount again to replace the pack.

For direct parsing without resource-managed I/O, success retains the caller's
blob pointer without copying or taking ownership. The caller keeps the bytes
valid and unchanged until unmount or shutdown and frees them afterward. A rejected
parse does not retain the supplied buffer. Resource-managed I/O owns its loaded
buffer and frees it on eviction or unmount.

NTPACK v3 uses the 16-bit `owner_entry` field in each 24-byte entry:

- Entry i owns its runtime object when owner_entry equals i.
- An alias points to an earlier entry which points to itself. Chains, forward
  references and cycles are invalid.
- An alias has the owner's asset type, offset and size. Its resource ID and
  metadata remain independent.
- Equal byte ranges do not imply sharing: separately self-owned entries are
  valid. Ownership is explicitly authored by the builder, never reconstructed
  by sorting or searching ranges at runtime.

The registry has a preallocated stack of free uint16 indices, initialized to
return low indices first. Unmount and virtual unregister return each live index
once. A parse reserves N indices; its reserved stack suffix maps manifest ordinals
to registry indices while filling records:

```text
old_top = free_count
free_count -= N
r(i) = free_assets[old_top - 1 - i]
assets[r(i)].owner_asset = r(entries[i].owner_entry)
```

Parsing invokes no callbacks while using that suffix and retains no pointer into
it. Every live record stores its translated owner index before the suffix can be
overwritten by later frees. Empty packs access no stack entries. Registration
needs no temporary allocation or capacity-sized WASM stack array; the persistent
index stack uses two bytes per configured asset slot (4 KiB at the default limit).
The existing resident metadata copy remains separate and survives blob eviction.

## Asset data access

With the pack blob resident, a validated entry's payload begins at
`blob + entry.offset` and spans `entry.size` bytes. Runtime activators borrow
that range. `nt_resource_get_blob` returns the bytes after `NtBlobAssetHeader`
for a published BLOB resource; its public contract defines the view's lifetime.

## Debugging

Builder includes the `nt_builder_dump_pack(pack_path)` utility that prints pack contents to console. No external tool needed.

## Future: partial loading

Flat layout allows HTTP Range requests: load first `header_size` bytes to get manifest, then load individual assets by offset/size on demand.
