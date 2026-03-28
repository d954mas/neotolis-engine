/* Benchmark: Basis ETC1S encoding strategies.
 *
 * Three strategies per scenario:
 *   Sequential — 1 texture at a time, Basis gets all threads
 *   Parallel   — N workers, Basis gets 1 thread each
 *   Adaptive   — N workers, Basis gets max(1, thread_count/work_count) each
 *
 * Build: cmake --build build/_cmake/native-debug --target bench_parallel
 * Run:   build/tests/native-debug/bench_parallel
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now_sec(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

#include "nt_basisu_encoder.h"
#include "stb_image.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include "stb_image_resize2.h"
#pragma clang diagnostic pop
#include "tinycthread.h"

#define MAX_JOBS 16
#define THREADS 6
#define TEXTURE_PATH "assets/textures/lenna.png"

typedef struct {
    uint8_t *pixels;
    uint32_t w;
    uint32_t h;
    uint32_t basis_threads;
    nt_basisu_encode_result_t result;
    double elapsed;
} TextureJob;

static int worker_func(void *arg) {
    TextureJob *job = (TextureJob *)arg;
    double t0 = now_sec();
    job->result = nt_basisu_encode_with_threads(
        job->basis_threads, job->pixels, job->w, job->h,
        true, false, 64, 1.5F, 1.25F, true);
    job->elapsed = now_sec() - t0;
    return 0;
}

static uint8_t *make_texture(const uint8_t *base, int bw, int bh, int tw, int th, int seed) {
    size_t out_bytes = (size_t)tw * (size_t)th * 4;
    uint8_t *out = (uint8_t *)malloc(out_bytes);
    if (tw == bw && th == bh) {
        memcpy(out, base, out_bytes);
    } else {
        stbir_resize_uint8_linear(base, bw, bh, 0, out, tw, th, 0, STBIR_RGBA);
    }
    for (int j = 0; j < 16; j++) {
        size_t off = ((size_t)(seed * 1000 + j * 37) % (size_t)(tw * th)) * 4;
        out[off + 0] ^= (uint8_t)(seed + 1);
        out[off + 1] ^= (uint8_t)(seed + 1);
    }
    return out;
}

static double run_sequential(TextureJob *jobs, int count, uint32_t basis_threads) {
    double t0 = now_sec();
    for (int i = 0; i < count; i++) {
        double t = now_sec();
        jobs[i].result = nt_basisu_encode_with_threads(
            basis_threads, jobs[i].pixels, jobs[i].w, jobs[i].h,
            true, false, 64, 1.5F, 1.25F, true);
        jobs[i].elapsed = now_sec() - t;
    }
    return now_sec() - t0;
}

static double run_parallel(TextureJob *jobs, int count) {
    thrd_t threads[MAX_JOBS];
    double t0 = now_sec();
    int spawned = 0;
    for (int i = 0; i < count; i++) {
        if (thrd_create(&threads[i], worker_func, &jobs[i]) == thrd_success) {
            spawned++;
        }
    }
    for (int i = 0; i < spawned; i++) {
        thrd_join(threads[i], NULL);
    }
    return now_sec() - t0;
}

static void print_jobs(TextureJob *jobs, int count) {
    for (int i = 0; i < count; i++) {
        printf("  [%2d] %4ux%-4u  pool(%u)  %.3fs  (%u bytes)\n",
               i, jobs[i].w, jobs[i].h, jobs[i].basis_threads,
               jobs[i].elapsed, jobs[i].result.size);
        nt_basisu_encode_free(&jobs[i].result);
    }
}

static void run_test(const char *name, TextureJob *jobs, int count) {
    printf("--- %s (%d textures) ---\n\n", name, count);

    /* 1. Sequential: all Basis threads per texture */
    for (int i = 0; i < count; i++) jobs[i].basis_threads = THREADS;
    printf("Sequential (job_pool(%d) per texture):\n", THREADS);
    double t_seq = run_sequential(jobs, count, THREADS);
    print_jobs(jobs, count);
    printf("  TOTAL: %.3fs\n\n", t_seq);

    /* 2. Parallel: 1 Basis thread per worker */
    for (int i = 0; i < count; i++) jobs[i].basis_threads = 1;
    printf("Parallel (%d workers x job_pool(1)):\n", count);
    double t_par = run_parallel(jobs, count);
    print_jobs(jobs, count);
    printf("  TOTAL: %.3fs\n\n", t_par);

    /* 3. Adaptive: distribute threads across workers */
    uint32_t adaptive_bt = THREADS / (uint32_t)count;
    if (adaptive_bt < 1) adaptive_bt = 1;
    for (int i = 0; i < count; i++) jobs[i].basis_threads = adaptive_bt;
    printf("Adaptive (%d workers x job_pool(%u)):\n", count, adaptive_bt);
    double t_adapt = run_parallel(jobs, count);
    print_jobs(jobs, count);
    printf("  TOTAL: %.3fs\n\n", t_adapt);

    /* Summary */
    double best = t_seq;
    const char *best_name = "sequential";
    if (t_par < best) { best = t_par; best_name = "parallel"; }
    if (t_adapt < best) { best = t_adapt; best_name = "adaptive"; }

    printf("  => Seq: %.3fs | Par: %.3fs | Adapt: %.3fs | Winner: %s\n\n",
           t_seq, t_par, t_adapt, best_name);
}

int main(void) {
    int bw = 0, bh = 0, ch = 0;
    uint8_t *base = stbi_load(TEXTURE_PATH, &bw, &bh, &ch, 4);
    if (!base) { fprintf(stderr, "Cannot load %s\n", TEXTURE_PATH); return 1; }

    nt_basisu_encoder_init();

    printf("=== Basis ETC1S-low Benchmark (budget: %d cores) ===\n\n", THREADS);

    /* Test 1: Uniform — 6 x 512x512 */
    {
        TextureJob jobs[6];
        for (int i = 0; i < 6; i++) {
            jobs[i].pixels = make_texture(base, bw, bh, 512, 512, i);
            jobs[i].w = 512; jobs[i].h = 512;
        }
        run_test("Uniform 6x512", jobs, 6);
        for (int i = 0; i < 6; i++) free(jobs[i].pixels);
    }

    /* Test 2: Mixed — 2 x 1024 + 8 x 256 */
    {
        TextureJob jobs[10];
        for (int i = 0; i < 2; i++) {
            jobs[i].pixels = make_texture(base, bw, bh, 1024, 1024, i + 100);
            jobs[i].w = 1024; jobs[i].h = 1024;
        }
        for (int i = 2; i < 10; i++) {
            jobs[i].pixels = make_texture(base, bw, bh, 256, 256, i + 200);
            jobs[i].w = 256; jobs[i].h = 256;
        }
        run_test("Mixed 2x1024 + 8x256", jobs, 10);
        for (int i = 0; i < 10; i++) free(jobs[i].pixels);
    }

    /* Test 3: Few large — 2 x 1024 */
    {
        TextureJob jobs[2];
        for (int i = 0; i < 2; i++) {
            jobs[i].pixels = make_texture(base, bw, bh, 1024, 1024, i + 400);
            jobs[i].w = 1024; jobs[i].h = 1024;
        }
        run_test("Few large 2x1024", jobs, 2);
        for (int i = 0; i < 2; i++) free(jobs[i].pixels);
    }

    /* Test 4: Single large — 1 x 1024 */
    {
        TextureJob jobs[1];
        jobs[0].pixels = make_texture(base, bw, bh, 1024, 1024, 300);
        jobs[0].w = 1024; jobs[0].h = 1024;
        run_test("Single 1x1024", jobs, 1);
        free(jobs[0].pixels);
    }

    stbi_image_free(base);
    nt_basisu_encoder_shutdown();
    return 0;
}
