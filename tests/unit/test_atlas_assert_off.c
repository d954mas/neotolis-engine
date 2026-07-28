/* Drives nt_atlas.c compiled with NT_ASSERT_MODE=0 (see nt_atlas_assert_off_tu.c)
 * — the shipping config. Every rejection below must come from the hard guards
 * alone: NT_ASSERT is ((void)0) there, so a deleted `return false` in the
 * validator goes unnoticed by the normal test binaries, whose asserts fire first. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0

#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "nt_atlas_format.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Minimal valid v7 blob: 1 region, `page_count` pages (each region needs a
 * backing page), 4 vertices, 6 indices, sections tightly packed. Offsets stay
 * consistent for ANY page_count so a cap test isolates the cap check alone. */
static uint32_t build_blob_pages(uint8_t *out, uint32_t cap, uint16_t page_count) {
    const uint32_t page_bytes = (uint32_t)page_count * (uint32_t)sizeof(uint64_t);
    const uint32_t vertex_offset = (uint32_t)sizeof(NtAtlasHeader) + page_bytes + (uint32_t)sizeof(NtAtlasRegion);
    const uint32_t index_offset = vertex_offset + (4U * (uint32_t)sizeof(NtAtlasVertex));
    const uint32_t total = index_offset + (6U * (uint32_t)sizeof(uint16_t));
    TEST_ASSERT_TRUE_MESSAGE(total <= cap, "mock blob buffer too small");
    memset(out, 0, total);

    NtAtlasHeader *hdr = (NtAtlasHeader *)out;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    hdr->region_count = 1;
    hdr->page_count = page_count;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = 4;
    hdr->index_offset = index_offset;
    hdr->total_index_count = 6;

    for (uint16_t pg = 0; pg < page_count; pg++) {
        const uint64_t tid = 0xA000ULL + pg;
        memcpy(out + sizeof(NtAtlasHeader) + ((size_t)pg * sizeof(uint64_t)), &tid, sizeof(uint64_t));
    }

    NtAtlasRegion *region = (NtAtlasRegion *)(out + sizeof(NtAtlasHeader) + page_bytes);
    region->name_hash = 0x0FFULL;
    region->source_w = 8;
    region->source_h = 8;
    region->vertex_start = 0;
    region->vertex_count = 4;
    region->index_start = 0;
    region->index_count = 6;
    region->page_index = 0;

    static const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(out + index_offset, indices, sizeof(indices));
    return total;
}

static uint32_t build_blob(uint8_t *out, uint32_t cap) { return build_blob_pages(out, cap, 1); }

void test_valid_blob_activates(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "a valid blob must activate with asserts off");
}

void test_bad_magic_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    ((NtAtlasHeader *)buf)->magic = 0xDEADBEEFU;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "bad magic must be a hard reject, not assert-only");
}

void test_bad_version_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    ((NtAtlasHeader *)buf)->version = NT_ATLAS_VERSION + 1;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "wrong version must be a hard reject, not assert-only");
}

void test_page_count_over_cap_rejected(void) {
    uint8_t buf[512];
    /* Layout is consistent for MAX+1 pages, so the cap guard is the ONLY reason
     * to reject — mutating page_count on a 1-page blob would also break the
     * section offsets and let any other check mask a deleted cap guard. */
    const uint32_t size = build_blob_pages(buf, sizeof(buf), NT_ATLAS_MAX_PAGES + 1);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "page_count past the cap must be a hard reject");
    /* Control: the same layout at exactly the cap is accepted. */
    const uint32_t ok_size = build_blob_pages(buf, sizeof(buf), NT_ATLAS_MAX_PAGES);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, ok_size), "exactly the cap must activate");
}

void test_truncated_blob_rejected(void) {
    uint8_t buf[512];
    (void)build_blob(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, (uint32_t)sizeof(NtAtlasHeader) - 1U), "a truncated blob must be a hard reject");
}

/* Regions sit after the page-id table in every blob this TU builds. */
static NtAtlasRegion *blob_region0(uint8_t *buf) { return (NtAtlasRegion *)(buf + sizeof(NtAtlasHeader) + (((size_t)((NtAtlasHeader *)buf)->page_count) * sizeof(uint64_t))); }

void test_page_index_unbacked_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    NtAtlasRegion *region = blob_region0(buf);
    /* Boundary: page_index == page_count is the first slot without a page. */
    region->page_index = 1;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "a region on an unbacked page must be a hard reject");
    region->page_index = NT_ATLAS_MAX_PAGES;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "page_index past the slot array must be a hard reject");
}

void test_vertex_span_oob_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    NtAtlasRegion *region = blob_region0(buf);
    region->vertex_start = 3;
    region->vertex_count = 4;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "a vertex span past total_vertex_count must be a hard reject");
}

void test_index_span_oob_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    NtAtlasRegion *region = blob_region0(buf);
    region->index_start = 5;
    region->index_count = 6;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "an index span past total_index_count must be a hard reject");
}

void test_vertex_offset_gap_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    ((NtAtlasHeader *)buf)->vertex_offset += 4;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "a non-canonical vertex_offset must be a hard reject");
}

void test_index_offset_gap_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    ((NtAtlasHeader *)buf)->index_offset += 4;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size), "a non-canonical index_offset must be a hard reject");
}

void test_truncated_index_section_rejected(void) {
    uint8_t buf[512];
    const uint32_t size = build_blob(buf, sizeof(buf));
    /* Blob ends mid-index-section: the span math must reject, not read past. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(buf, size - 2U), "an index section past the blob end must be a hard reject");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_blob_activates);
    RUN_TEST(test_bad_magic_rejected);
    RUN_TEST(test_bad_version_rejected);
    RUN_TEST(test_page_count_over_cap_rejected);
    RUN_TEST(test_truncated_blob_rejected);
    RUN_TEST(test_page_index_unbacked_rejected);
    RUN_TEST(test_vertex_span_oob_rejected);
    RUN_TEST(test_index_span_oob_rejected);
    RUN_TEST(test_vertex_offset_gap_rejected);
    RUN_TEST(test_index_offset_gap_rejected);
    RUN_TEST(test_truncated_index_section_rejected);
    return UNITY_END();
}
