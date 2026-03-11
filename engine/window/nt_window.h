#ifndef NT_WINDOW_H
#define NT_WINDOW_H

#include "core/nt_types.h"

/* ---- Window / canvas state ---- */

typedef struct nt_window_t {
    /* Config (game writes before init) */
    float max_dpr; /* Default 2.0, effective DPR = min(device_dpr, max_dpr) */

    /* State (engine writes, game reads) */
    uint32_t width;     /* Canvas CSS width (pixels) */
    uint32_t height;    /* Canvas CSS height (pixels) */
    uint32_t fb_width;  /* Framebuffer width (render pixels) */
    uint32_t fb_height; /* Framebuffer height (render pixels) */
    float dpr;          /* Effective DPR = min(device_dpr, max_dpr) */
} nt_window_t;

extern nt_window_t g_nt_window;

/* ---- Lifecycle (platform-specific, see Plan 02) ---- */

void nt_window_init(void);
void nt_window_poll(void);
void nt_window_shutdown(void);

/* ---- Shared DPR math (used by all platform backends) ---- */

void nt_window_apply_sizes(float css_w, float css_h, float device_dpr);

#endif /* NT_WINDOW_H */
