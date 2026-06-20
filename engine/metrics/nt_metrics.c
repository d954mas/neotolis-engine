#include "metrics/nt_metrics.h"

#if NT_METRICS_ENABLED

/* RED skeleton: API present + linkable, behavior NOT yet implemented so the L1
 * test fails on its assertions (TDD red gate). Filled in the green commit. */

void nt_metrics_init(void) {}
void nt_metrics_reset(void) {}
void nt_metrics_sample(void) {}

void nt_metrics_channel_stats(nt_metrics_channel_t channel, nt_metrics_stats_t *out) {
    (void)channel;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}

double nt_metrics_fps_low_1pct(void) { return 0.0; }
double nt_metrics_fps_low_01pct(void) { return 0.0; }
double nt_metrics_over_budget_pct(double budget_ms) {
    (void)budget_ms;
    return 0.0;
}

uint16_t nt_metrics_user_count(void) { return 0; }
const char *nt_metrics_user_name(uint16_t i) {
    (void)i;
    return NULL;
}
void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out) {
    (void)i;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}

#ifdef NT_TEST_ACCESS
void nt_metrics_test_push(nt_metrics_channel_t channel, double value) {
    (void)channel;
    (void)value;
}
#endif

#else /* NT_METRICS_ENABLED == 0 — zero-footprint no-op stubs for release/OFF mirror */

void nt_metrics_init(void) {}
void nt_metrics_reset(void) {}
void nt_metrics_sample(void) {}
void nt_metrics_channel_stats(nt_metrics_channel_t channel, nt_metrics_stats_t *out) {
    (void)channel;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}
double nt_metrics_fps_low_1pct(void) { return 0.0; }
double nt_metrics_fps_low_01pct(void) { return 0.0; }
double nt_metrics_over_budget_pct(double budget_ms) {
    (void)budget_ms;
    return 0.0;
}
uint16_t nt_metrics_user_count(void) { return 0; }
const char *nt_metrics_user_name(uint16_t i) {
    (void)i;
    return NULL;
}
void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out) {
    (void)i;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}

#endif /* NT_METRICS_ENABLED */
