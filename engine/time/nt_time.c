#include "time/nt_time.h"
#include "core/nt_platform.h"

/* ---- Accumulator (platform-agnostic math) ---- */

void nt_accumulator_init(nt_accumulator_t *acc, float fixed_dt, int max_steps) {
    acc->fixed_dt = fixed_dt;
    acc->accumulator = 0.0F;
    acc->max_steps = max_steps;
    acc->steps_this_frame = 0;
}

void nt_accumulator_add(nt_accumulator_t *acc, float dt) {
    acc->accumulator += dt;
    acc->steps_this_frame = 0;
}

bool nt_accumulator_step(nt_accumulator_t *acc) {
    if (acc->accumulator >= acc->fixed_dt && acc->steps_this_frame < acc->max_steps) {
        acc->accumulator -= acc->fixed_dt;
        acc->steps_this_frame++;
        return true;
    }
    return false;
}

/* ---- Platform timer ---- */

#ifdef NT_PLATFORM_WEB
#include <emscripten.h>

double nt_time_now(void) { return emscripten_get_now() / 1000.0; /* ms -> seconds */ }

uint64_t nt_time_nanos(void) { return (uint64_t)(emscripten_get_now() * 1000000.0); /* ms -> ns */ }

#elif defined(NT_PLATFORM_WIN)
#include <windows.h>

static double s_freq_inv = 0.0;

static void nt_time_ensure_freq(void) {
    if (s_freq_inv == 0.0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_freq_inv = 1.0 / (double)freq.QuadPart;
    }
}

double nt_time_now(void) {
    nt_time_ensure_freq();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * s_freq_inv;
}

uint64_t nt_time_nanos(void) {
    nt_time_ensure_freq();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((double)counter.QuadPart * s_freq_inv * 1e9);
}

#else /* NT_PLATFORM_NATIVE (Linux, macOS, POSIX) */
#include <time.h>

double nt_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

uint64_t nt_time_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#endif
