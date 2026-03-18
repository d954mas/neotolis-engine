# Hash Algorithm Benchmark Results

**Date:** 2026-03-19
**Benchmark source:** `tools/research/hash_benchmark/main.c` (official xxHash via `deps/xxhash/xxhash.h`)
**Iterations:** 1,000,000 per test

## Candidates

| Candidate | Implementation | Notes |
|-----------|---------------|-------|
| FNV-1a | Inline, byte-by-byte | Previous engine default |
| XXH32/XXH64 | Official Cyan4973/xxHash | **Selected for engine** |
| XXH3 | Official Cyan4973/xxHash | 128-bit multiply, SIMD paths |
| MurmurHash3 | x86_32 + x64_128 (upper 64) | No standalone 64-bit variant |
| CRC32 | Lookup table (same as nt_crc32) | Error detection, not identity |

## Native Release (Windows 11 x86_64, Clang 18, -O2)

### 64-bit Throughput (primary use case: resource_id)

| Candidate | Short 18B | Medium 22B | Long 85B |
|-----------|-----------|------------|----------|
| **XXH3** | **551M** | **562M** | **330M** |
| **XXH64** | **295M** | **250M** | **116M** |
| MurmurHash3 x64 | 253M | 225M | 95M |
| FNV-1a | 223M | 182M | 28M |
| CRC32 | 127M | 97M | 16M |

### 32-bit Throughput (pack_id, attribute names)

| Candidate | Short 18B | Medium 22B | Long 85B |
|-----------|-----------|------------|----------|
| **XXH3** | **571M** | **553M** | **338M** |
| XXH32 | 261M | 163M | 89M |
| FNV-1a | 230M | 169M | 28M |
| MurmurHash3 x86 | 224M | 199M | 48M |
| CRC32 | 92M | 70M | 10M |

## WASM (Emscripten, -O2, Node.js V8)

### 64-bit Throughput

| Candidate | Short 18B | Medium 22B | Long 85B |
|-----------|-----------|------------|----------|
| XXH3 | 207M | 210M | 97M |
| **XXH64** | **189M** | **195M** | **90M** |
| MurmurHash3 x64 | 187M | 158M | 78M |
| FNV-1a | 135M | 111M | 26M |
| CRC32 | 68M | 54M | 12M |

### 32-bit Throughput

| Candidate | Short 18B | Medium 22B | Long 85B |
|-----------|-----------|------------|----------|
| MurmurHash3 x86 | **181M** | 184M | 54M |
| XXH3 | 159M | 178M | 99M |
| **XXH32** | 157M | **191M** | **100M** |
| FNV-1a | 131M | 116M | 26M |
| CRC32 | 55M | 43M | 9M |

## Distribution Quality

Chi-squared test: 10,000 keys hashed into 256 buckets (power-of-2, worst case for poor avalanche).
Key pattern: `assets/textures/env/tile_NNNN.ntex`
Ideal chi2 ~ 256.

| Candidate | chi2 32-bit | chi2 64-bit | 32-bit bucket range |
|-----------|-------------|-------------|---------------------|
| MurmurHash3 | **255.5** | 282.6 | 21-59 |
| XXH32/64 | **263.9** | **218.8** | 25-61 |
| XXH3 | 265.5 | 265.5 | 22-62 |
| FNV-1a | **524.2** | 71.2 | 23-59 |
| CRC32 | 25.0 | **2,550,000** | 36-42 (32b) / 0-10000 (64b) |

FNV-1a 32-bit chi2=524 is 2x worse than ideal -- significant clustering in open-addressing hash maps.

## Collision Test

10,000 sequential keys (`key_0000` through `key_9999`):

All candidates: 0 collisions (32-bit and 64-bit).

## WASM Binary Size Impact

| Metric | FNV-1a (baseline) | XXH32/XXH64 | Delta |
|--------|-------------------|-------------|-------|
| Raw .wasm | 53,836 B | 56,350 B | +2,514 B |
| Gzipped .wasm | 26,437 B | 27,051 B | **+614 B** |

Measured on `textured_quad` example (only example that links nt_hash via nt_resource).
Other examples unchanged (LTO removes unused hash code).

## Decision: XXH32/XXH64

**Rationale:**

1. **Distribution quality:** FNV-1a fails SMHasher avalanche tests (100% worst bias). XXH32/XXH64 pass all SMHasher tests. This directly impacts open-addressing hash map performance in the engine.
2. **WASM throughput:** XXH64 is 3.5x faster than FNV-1a on long strings (90M vs 26M ops/s). XXH32 is 4x faster (100M vs 26M). On short strings ~1.4x faster.
3. **No 128-bit multiply:** Unlike XXH3/wyhash/rapidhash, XXH32/XXH64 use only standard `i32.mul`/`i64.mul` -- native WASM instructions. No penalty from missing `i64.mul_wide_u` proposal.
4. **Minimal size impact:** +614 bytes gzipped (~2.3% of a typical example binary).
5. **Battle-tested:** xxHash is used in Linux kernel, FreeBSD, Chromium, LZ4, Zstandard.
6. **Single vendored header:** `deps/xxhash/xxhash.h` from Cyan4973/xxHash, `XXH_INLINE_ALL` + `XXH_NO_XXH3`.

**Why not XXH3:** Uses 128-bit multiply and SIMD paths. On native x86_64 it's 2x faster than XXH64, but on WASM they're nearly equal due to 128-bit multiply emulation. Extra complexity for no WASM benefit.

**Why not wyhash/rapidhash:** Same 128-bit multiply problem. Better candidates once browsers ship the WebAssembly wide-arithmetic proposal.
