#include "time/nt_time.h"
#include "core/nt_platform.h"

/* ---- Accumulator (platform-agnostic math) ---- */

void nt_accumulator_init(nt_accumulator_t *acc, float fixed_dt, int max_steps) {
    acc->fixed_dt = fixed_dt;
    acc->accumulator = 0.0F;
    acc->max_steps = max_steps;
}

int nt_accumulator_update(nt_accumulator_t *acc, float dt) {
    acc->accumulator += dt;
    int steps = 0;
    while (acc->accumulator >= acc->fixed_dt && steps < acc->max_steps) {
        acc->accumulator -= acc->fixed_dt;
        steps++;
    }
    return steps;
}

/* ---- Platform timer ---- */

#ifdef NT_PLATFORM_WEB
#include <emscripten.h>

double nt_time_now(void) { return emscripten_get_now() / 1000.0; /* ms -> seconds */ }

uint64_t nt_time_nanos(void) { return (uint64_t)(emscripten_get_now() * 1000000.0); /* ms -> ns */ }

void nt_time_sleep(double seconds) { (void)seconds; /* No-op on web -- RAF controls timing */ }

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

void nt_time_sleep(double seconds) {
    if (seconds <= 0.0) return;
    /* Set 1ms timer resolution on first call (standard for games) */
    static bool s_period_set = false;
    if (!s_period_set) {
        timeBeginPeriod(1);
        s_period_set = true;
    }
    DWORD ms = (DWORD)(seconds * 1000.0);
    if (ms > 0) Sleep(ms);
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

void nt_time_sleep(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

#endif
