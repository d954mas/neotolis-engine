// NOLINTBEGIN(concurrency-mt-unsafe,cert-msc30-c,cert-msc50-cpp,cert-msc32-c,cert-msc51-cpp)
/**
 * WASM sort benchmark — qsort vs radix sort.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/nt_render_defs.h"
#include "sort/nt_sort.h"
#include "time/nt_time.h"

/* Instantiate sort locally (WASM benchmark does not link nt_render) */
NT_SORT_DEFINE(bench_sort, nt_render_item_t)

#define BENCH_MAX 4096
#define BENCH_ITERS 200

static nt_render_item_t s_items[BENCH_MAX];
static nt_render_item_t s_scratch[BENCH_MAX];
static nt_render_item_t s_backup[BENCH_MAX];

static int compare_render_items(const void *a, const void *b) {
    const nt_render_item_t *ia = (const nt_render_item_t *)a;
    const nt_render_item_t *ib = (const nt_render_item_t *)b;
    return (ia->sort_key > ib->sort_key) - (ia->sort_key < ib->sort_key);
}

static int bench_size(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        s_items[i].sort_key = ((uint64_t)(unsigned)rand() << 32) | (uint64_t)(unsigned)rand();
        s_items[i].entity = i;
        s_items[i].batch_key = (uint32_t)(rand() % 50);
    }
    memcpy(s_backup, s_items, count * sizeof(nt_render_item_t));

    /* qsort */
    double qstart = nt_time_now();
    for (int iter = 0; iter < BENCH_ITERS; ++iter) {
        memcpy(s_items, s_backup, count * sizeof(nt_render_item_t));
        qsort(s_items, count, sizeof(nt_render_item_t), compare_render_items);
    }
    double qms = ((nt_time_now() - qstart) / BENCH_ITERS) * 1000.0;

    /* radix sort */
    double rstart = nt_time_now();
    for (int iter = 0; iter < BENCH_ITERS; ++iter) {
        memcpy(s_items, s_backup, count * sizeof(nt_render_item_t));
        bench_sort(s_items, count, s_scratch);
    }
    double rms = ((nt_time_now() - rstart) / BENCH_ITERS) * 1000.0;

    double speedup = (rms > 0.0) ? (qms / rms) : 0.0;
    printf("  N=%u: qsort=%.4fms radix=%.4fms speedup=%.2fx\n", count, qms, rms, speedup);

    /* Verify correctness */
    memcpy(s_items, s_backup, count * sizeof(nt_render_item_t));
    qsort(s_backup, count, sizeof(nt_render_item_t), compare_render_items);
    bench_sort(s_items, count, s_scratch);

    for (uint32_t i = 0; i < count; ++i) {
        if (s_backup[i].sort_key != s_items[i].sort_key) {
            printf("  MISMATCH at %u\n", i);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    srand(42);
    printf("WASM sort benchmark (iters=%d)\n", BENCH_ITERS);

    int fail = 0;
    fail |= bench_size(256);
    fail |= bench_size(1000);
    fail |= bench_size(2000);
    fail |= bench_size(4096);

    if (fail) {
        printf("BENCHMARK FAILED\n");
        return 1;
    }
    printf("All passed.\n");
    return 0;
}
// NOLINTEND(concurrency-mt-unsafe,cert-msc30-c,cert-msc50-cpp,cert-msc32-c,cert-msc51-cpp)
