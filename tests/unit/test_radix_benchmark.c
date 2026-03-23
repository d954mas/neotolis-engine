// NOLINTBEGIN(concurrency-mt-unsafe,cert-msc30-c,cert-msc50-cpp)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/nt_render_defs.h"
#include "sort/nt_sort.h"
#include "time/nt_time.h"
#include "unity.h"

/* ---- setUp / tearDown (benchmark has no global state) ---- */

void setUp(void) {}
void tearDown(void) {}

/* ---- Static arrays to avoid stack overflow at large sizes ---- */

#define BENCH_MAX 4096
#define BENCH_ITERS 500

static nt_render_item_t s_items[BENCH_MAX];
static nt_render_item_t s_scratch[BENCH_MAX];
static nt_render_item_t s_backup[BENCH_MAX];

/* ---- qsort comparator (same as current render module) ---- */

static int compare_render_items(const void *a, const void *b) {
    const nt_render_item_t *ia = (const nt_render_item_t *)a;
    const nt_render_item_t *ib = (const nt_render_item_t *)b;
    return (ia->sort_key > ib->sort_key) - (ia->sort_key < ib->sort_key);
}

/* ---- Benchmark a single item count ---- */

static void bench_size(uint32_t count) {
    /* Generate random items */
    for (uint32_t i = 0; i < count; ++i) {
        s_items[i].sort_key = ((uint64_t)(unsigned)rand() << 32) | (uint64_t)(unsigned)rand();
        s_items[i].entity = i;
        s_items[i].batch_key = (uint32_t)(rand() % 50);
    }
    memcpy(s_backup, s_items, count * sizeof(nt_render_item_t));

    /* Time qsort */
    double qsort_start = nt_time_now();
    for (int iter = 0; iter < BENCH_ITERS; ++iter) {
        memcpy(s_items, s_backup, count * sizeof(nt_render_item_t));
        qsort(s_items, count, sizeof(nt_render_item_t), compare_render_items);
    }
    double qsort_end = nt_time_now();
    double qsort_ms = ((qsort_end - qsort_start) / BENCH_ITERS) * 1000.0;

    /* Time radix sort */
    double radix_start = nt_time_now();
    for (int iter = 0; iter < BENCH_ITERS; ++iter) {
        memcpy(s_items, s_backup, count * sizeof(nt_render_item_t));
        nt_sort_by_key(s_items, count, s_scratch);
    }
    double radix_end = nt_time_now();
    double radix_ms = ((radix_end - radix_start) / BENCH_ITERS) * 1000.0;

    double speedup = (radix_ms > 0.0) ? (qsort_ms / radix_ms) : 0.0;
    printf("  N=%u: qsort=%.4fms radix=%.4fms speedup=%.2fx\n", count, qsort_ms, radix_ms, speedup);

    /* Verify correctness: sort backup with qsort, sort items with radix, compare */
    memcpy(s_items, s_backup, count * sizeof(nt_render_item_t));
    qsort(s_backup, count, sizeof(nt_render_item_t), compare_render_items);
    nt_sort_by_key(s_items, count, s_scratch);

    for (uint32_t i = 0; i < count; ++i) {
        TEST_ASSERT_EQUAL_UINT64(s_backup[i].sort_key, s_items[i].sort_key);
    }
}

/* ---- Benchmark test functions ---- */

void test_benchmark_256(void) { bench_size(256); }
void test_benchmark_1000(void) { bench_size(1000); }
void test_benchmark_2000(void) { bench_size(2000); }
void test_benchmark_4096(void) { bench_size(4096); }

/* ---- Main ---- */

int main(void) {
    srand((unsigned)nt_time_nanos());
    printf("Radix sort benchmark (iters=%d)\n", BENCH_ITERS);
    UNITY_BEGIN();
    RUN_TEST(test_benchmark_256);
    RUN_TEST(test_benchmark_1000);
    RUN_TEST(test_benchmark_2000);
    RUN_TEST(test_benchmark_4096);
    return UNITY_END();
}
// NOLINTEND(concurrency-mt-unsafe,cert-msc30-c,cert-msc50-cpp)
