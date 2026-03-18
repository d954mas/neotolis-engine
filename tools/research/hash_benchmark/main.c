/*
 * Hash Algorithm Benchmark
 *
 * Compares 4 candidates for the nt_hash module:
 *   1. FNV-1a (32-bit and 64-bit)
 *   2. xxHash (xxh32 and xxh64, minimal byte-by-byte impl)
 *   3. MurmurHash3 (x86_32 and x86_128, take upper 64 bits)
 *   4. CRC32-as-hash (lookup table, same as shared/nt_crc32.c)
 *
 * Metrics: throughput on short/medium strings, collision count on 10k keys.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/* ================================================================
 *  Timing
 * ================================================================ */

static double get_time_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

/* ================================================================
 *  1. FNV-1a
 * ================================================================ */

static uint32_t fnv1a_32(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x811C9DC5U;
    for (uint32_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x01000193U;
    }
    return h;
}

static uint64_t fnv1a_64(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 0x00000100000001B3ULL;
    }
    return h;
}

/* ================================================================
 *  2. xxHash (minimal byte-by-byte, correct but not optimized)
 * ================================================================ */

#define XXH_PRIME32_1 0x9E3779B1U
#define XXH_PRIME32_2 0x85EBCA77U
#define XXH_PRIME32_3 0xC2B2AE3DU
#define XXH_PRIME32_4 0x27D4EB2FU
#define XXH_PRIME32_5 0x165667B1U

static uint32_t xxh32_rotl(uint32_t v, int r) { return (v << r) | (v >> (32 - r)); }

static uint32_t xxhash_32(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + size;
    uint32_t h;

    if (size >= 16) {
        const uint8_t *limit = end - 16;
        uint32_t v1 = 0 + XXH_PRIME32_1 + XXH_PRIME32_2;
        uint32_t v2 = 0 + XXH_PRIME32_2;
        uint32_t v3 = 0;
        uint32_t v4 = 0 - XXH_PRIME32_1;
        do {
            uint32_t k;
            memcpy(&k, p, 4);
            v1 += k * XXH_PRIME32_2;
            v1 = xxh32_rotl(v1, 13) * XXH_PRIME32_1;
            p += 4;
            memcpy(&k, p, 4);
            v2 += k * XXH_PRIME32_2;
            v2 = xxh32_rotl(v2, 13) * XXH_PRIME32_1;
            p += 4;
            memcpy(&k, p, 4);
            v3 += k * XXH_PRIME32_2;
            v3 = xxh32_rotl(v3, 13) * XXH_PRIME32_1;
            p += 4;
            memcpy(&k, p, 4);
            v4 += k * XXH_PRIME32_2;
            v4 = xxh32_rotl(v4, 13) * XXH_PRIME32_1;
            p += 4;
        } while (p <= limit);
        h = xxh32_rotl(v1, 1) + xxh32_rotl(v2, 7) + xxh32_rotl(v3, 12) + xxh32_rotl(v4, 18);
    } else {
        h = 0 + XXH_PRIME32_5;
    }

    h += size;

    while (p + 4 <= end) {
        uint32_t k;
        memcpy(&k, p, 4);
        h += k * XXH_PRIME32_3;
        h = xxh32_rotl(h, 17) * XXH_PRIME32_4;
        p += 4;
    }
    while (p < end) {
        h += (*p) * XXH_PRIME32_5;
        h = xxh32_rotl(h, 11) * XXH_PRIME32_1;
        p++;
    }

    h ^= h >> 15;
    h *= XXH_PRIME32_2;
    h ^= h >> 13;
    h *= XXH_PRIME32_3;
    h ^= h >> 16;
    return h;
}

#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL

static uint64_t xxh64_rotl(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }

static uint64_t xxh64_round(uint64_t acc, uint64_t input) {
    acc += input * XXH_PRIME64_2;
    acc = xxh64_rotl(acc, 31);
    acc *= XXH_PRIME64_1;
    return acc;
}

static uint64_t xxh64_merge_round(uint64_t acc, uint64_t val) {
    val = xxh64_round(0, val);
    acc ^= val;
    acc = acc * XXH_PRIME64_1 + XXH_PRIME64_4;
    return acc;
}

static uint64_t xxhash_64(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + size;
    uint64_t h;

    if (size >= 32) {
        const uint8_t *limit = end - 32;
        uint64_t v1 = 0 + XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = 0 + XXH_PRIME64_2;
        uint64_t v3 = 0;
        uint64_t v4 = 0 - XXH_PRIME64_1;
        do {
            uint64_t k;
            memcpy(&k, p, 8);
            v1 = xxh64_round(v1, k);
            p += 8;
            memcpy(&k, p, 8);
            v2 = xxh64_round(v2, k);
            p += 8;
            memcpy(&k, p, 8);
            v3 = xxh64_round(v3, k);
            p += 8;
            memcpy(&k, p, 8);
            v4 = xxh64_round(v4, k);
            p += 8;
        } while (p <= limit);
        h = xxh64_rotl(v1, 1) + xxh64_rotl(v2, 7) + xxh64_rotl(v3, 12) + xxh64_rotl(v4, 18);
        h = xxh64_merge_round(h, v1);
        h = xxh64_merge_round(h, v2);
        h = xxh64_merge_round(h, v3);
        h = xxh64_merge_round(h, v4);
    } else {
        h = 0 + XXH_PRIME64_5;
    }

    h += (uint64_t)size;

    while (p + 8 <= end) {
        uint64_t k;
        memcpy(&k, p, 8);
        k = xxh64_round(0, k);
        h ^= k;
        h = xxh64_rotl(h, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        p += 8;
    }
    while (p + 4 <= end) {
        uint32_t k32;
        memcpy(&k32, p, 4);
        h ^= (uint64_t)k32 * XXH_PRIME64_1;
        h = xxh64_rotl(h, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        p += 4;
    }
    while (p < end) {
        h ^= (*p) * XXH_PRIME64_5;
        h = xxh64_rotl(h, 11) * XXH_PRIME64_1;
        p++;
    }

    h ^= h >> 33;
    h *= XXH_PRIME64_2;
    h ^= h >> 29;
    h *= XXH_PRIME64_3;
    h ^= h >> 32;
    return h;
}

/* ================================================================
 *  3. MurmurHash3
 *     x86_32 (32-bit output) and x86_128 (take upper 64 bits)
 *     Based on PeterScott public domain C port.
 * ================================================================ */

static uint32_t murmur_rotl32(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

static uint32_t murmur_fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85EBCA6BU;
    h ^= h >> 13;
    h *= 0xC2B2AE35U;
    h ^= h >> 16;
    return h;
}

static uint32_t murmur3_x86_32(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t nblocks = size / 4;
    uint32_t h1 = 0; /* seed = 0 */
    const uint32_t c1 = 0xCC9E2D51U;
    const uint32_t c2 = 0x1B873593U;

    for (uint32_t i = 0; i < nblocks; i++) {
        uint32_t k1;
        memcpy(&k1, p + i * 4, 4);
        k1 *= c1;
        k1 = murmur_rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1 = murmur_rotl32(h1, 13);
        h1 = h1 * 5 + 0xE6546B64U;
    }

    const uint8_t *tail = p + nblocks * 4;
    uint32_t k1 = 0;
    switch (size & 3) {
    case 3:
        k1 ^= (uint32_t)tail[2] << 16;
        /* fallthrough */
    case 2:
        k1 ^= (uint32_t)tail[1] << 8;
        /* fallthrough */
    case 1:
        k1 ^= tail[0];
        k1 *= c1;
        k1 = murmur_rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
    }

    h1 ^= size;
    h1 = murmur_fmix32(h1);
    return h1;
}

static uint64_t murmur_fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xFF51AFD7ED558CCDULL;
    k ^= k >> 33;
    k *= 0xC4CEB9FE1A85EC53ULL;
    k ^= k >> 33;
    return k;
}

static uint64_t murmur_rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

static uint64_t murmur3_x64_128_upper64(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t nblocks = size / 16;
    uint64_t h1 = 0; /* seed = 0 */
    uint64_t h2 = 0;
    const uint64_t c1 = 0x87C37B91114253D5ULL;
    const uint64_t c2 = 0x4CF5AD432745937FULL;

    for (uint32_t i = 0; i < nblocks; i++) {
        uint64_t k1;
        uint64_t k2;
        memcpy(&k1, p + i * 16, 8);
        memcpy(&k2, p + i * 16 + 8, 8);

        k1 *= c1;
        k1 = murmur_rotl64(k1, 31);
        k1 *= c2;
        h1 ^= k1;
        h1 = murmur_rotl64(h1, 27);
        h1 += h2;
        h1 = h1 * 5 + 0x52DCE729U;

        k2 *= c2;
        k2 = murmur_rotl64(k2, 33);
        k2 *= c1;
        h2 ^= k2;
        h2 = murmur_rotl64(h2, 31);
        h2 += h1;
        h2 = h2 * 5 + 0x38495AB5U;
    }

    const uint8_t *tail = p + nblocks * 16;
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    switch (size & 15) {
    case 15:
        k2 ^= (uint64_t)tail[14] << 48;
        /* fallthrough */
    case 14:
        k2 ^= (uint64_t)tail[13] << 40;
        /* fallthrough */
    case 13:
        k2 ^= (uint64_t)tail[12] << 32;
        /* fallthrough */
    case 12:
        k2 ^= (uint64_t)tail[11] << 24;
        /* fallthrough */
    case 11:
        k2 ^= (uint64_t)tail[10] << 16;
        /* fallthrough */
    case 10:
        k2 ^= (uint64_t)tail[9] << 8;
        /* fallthrough */
    case 9:
        k2 ^= (uint64_t)tail[8];
        k2 *= c2;
        k2 = murmur_rotl64(k2, 33);
        k2 *= c1;
        h2 ^= k2;
        /* fallthrough */
    case 8:
        k1 ^= (uint64_t)tail[7] << 56;
        /* fallthrough */
    case 7:
        k1 ^= (uint64_t)tail[6] << 48;
        /* fallthrough */
    case 6:
        k1 ^= (uint64_t)tail[5] << 40;
        /* fallthrough */
    case 5:
        k1 ^= (uint64_t)tail[4] << 32;
        /* fallthrough */
    case 4:
        k1 ^= (uint64_t)tail[3] << 24;
        /* fallthrough */
    case 3:
        k1 ^= (uint64_t)tail[2] << 16;
        /* fallthrough */
    case 2:
        k1 ^= (uint64_t)tail[1] << 8;
        /* fallthrough */
    case 1:
        k1 ^= (uint64_t)tail[0];
        k1 *= c1;
        k1 = murmur_rotl64(k1, 31);
        k1 *= c2;
        h1 ^= k1;
    }

    h1 ^= (uint64_t)size;
    h2 ^= (uint64_t)size;
    h1 += h2;
    h2 += h1;
    h1 = murmur_fmix64(h1);
    h2 = murmur_fmix64(h2);
    h1 += h2;
    /* h2 += h1; -- not needed, we only return h1 (upper 64 bits) */
    return h1;
}

/* ================================================================
 *  4. CRC32-as-hash (same lookup table as shared/nt_crc32.c)
 * ================================================================ */

/* clang-format off */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};
/* clang-format on */

static uint32_t crc32_hash(const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

/* CRC32 pseudo-64-bit: hash first half XOR hash second half */
static uint64_t crc32_hash64(const void *data, uint32_t size) {
    uint32_t half = size / 2;
    const uint8_t *p = (const uint8_t *)data;
    uint32_t lo = crc32_hash(p, half);
    uint32_t hi = crc32_hash(p + half, size - half);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* ================================================================
 *  Benchmark harness
 * ================================================================ */

#define ITERATIONS 1000000

typedef uint32_t (*hash32_fn)(const void *, uint32_t);
typedef uint64_t (*hash64_fn)(const void *, uint32_t);

typedef struct {
    const char *name;
    hash32_fn fn32;
    hash64_fn fn64;
} Candidate;

static void bench_throughput(const Candidate *c, const char *input, uint32_t len, const char *label) {
    volatile uint32_t sink32 = 0;
    volatile uint64_t sink64 = 0;

    /* 32-bit */
    double t0 = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        sink32 = c->fn32(input, len);
    }
    double t1 = get_time_sec();
    double ms32 = (t1 - t0) * 1000.0;
    double ops32 = (double)ITERATIONS / (t1 - t0);

    /* 64-bit */
    t0 = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        sink64 = c->fn64(input, len);
    }
    t1 = get_time_sec();
    double ms64 = (t1 - t0) * 1000.0;
    double ops64 = (double)ITERATIONS / (t1 - t0);

    (void)sink32;
    (void)sink64;

    printf("  %-14s  %-20s  32-bit: %7.1f ms (%10.0f ops/s)  64-bit: %7.1f ms (%10.0f ops/s)\n", c->name, label, ms32, ops32, ms64, ops64);
}

/* ================================================================
 *  Collision test: 10000 sequential strings
 * ================================================================ */

#define COLLISION_COUNT 10000

static uint32_t collision_test_32(hash32_fn fn) {
    uint32_t hashes[COLLISION_COUNT];
    char buf[32];
    for (int i = 0; i < COLLISION_COUNT; i++) {
        int len = snprintf(buf, sizeof(buf), "key_%04d", i);
        hashes[i] = fn(buf, (uint32_t)len);
    }
    /* Count collisions (O(n^2) but n is small) */
    uint32_t collisions = 0;
    for (int i = 0; i < COLLISION_COUNT; i++) {
        for (int j = i + 1; j < COLLISION_COUNT; j++) {
            if (hashes[i] == hashes[j]) {
                collisions++;
            }
        }
    }
    return collisions;
}

/* ================================================================
 *  Main
 * ================================================================ */

int main(void) {
    Candidate candidates[] = {
        {"FNV-1a", fnv1a_32, fnv1a_64},
        {"xxHash", xxhash_32, xxhash_64},
        {"MurmurHash3", murmur3_x86_32, murmur3_x64_128_upper64},
        {"CRC32", crc32_hash, crc32_hash64},
    };
    int num = (int)(sizeof(candidates) / sizeof(candidates[0]));

    const char *short1 = "textures/lenna.png";
    const char *short2 = "assets/meshes/cube.glb";
    const char *medium = "assets/textures/environments/outdoor/sky_clouds_morning_hdr_compressed_bc6h_v2.ntex";

    printf("=== Hash Algorithm Benchmark ===\n");
    printf("Iterations: %d per test\n\n", ITERATIONS);

    printf("--- Throughput ---\n");
    for (int i = 0; i < num; i++) {
        bench_throughput(&candidates[i], short1, (uint32_t)strlen(short1), short1);
        bench_throughput(&candidates[i], short2, (uint32_t)strlen(short2), short2);
        bench_throughput(&candidates[i], medium, (uint32_t)strlen(medium), medium);
        printf("\n");
    }

    printf("--- Collision Test (10000 sequential keys, 32-bit) ---\n");
    for (int i = 0; i < num; i++) {
        uint32_t col = collision_test_32(candidates[i].fn32);
        printf("  %-14s  collisions: %u / %d\n", candidates[i].name, col, COLLISION_COUNT);
    }

    printf("\n--- Approximate Code Size (source lines) ---\n");
    printf("  FNV-1a:       ~15 lines (32+64)\n");
    printf("  xxHash:       ~90 lines (32+64)\n");
    printf("  MurmurHash3:  ~110 lines (32+128)\n");
    printf("  CRC32:        ~75 lines (table + hash)\n");

    printf("\nDone.\n");
    return 0;
}
