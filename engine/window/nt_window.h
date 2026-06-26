#ifndef NT_WINDOW_H
#define NT_WINDOW_H

#include "core/nt_types.h"

/* ---- Vsync mode (used by nt_window_set_vsync and nt_app_t) ---- */

typedef enum {
    NT_VSYNC_OFF = 0,
    NT_VSYNC_ON = 1,
    NT_VSYNC_ADAPTIVE = 2,
} nt_vsync_t;

/* ---- Window / canvas state ---- */

typedef struct nt_window_t {
    /* Config (game writes before init) */
    float max_dpr;     /* Default 2.0, effective DPR = min(device_dpr, max_dpr) */
    const char *title; /* Window title, default "Neotolis" */
    bool resizable;    /* Default true on desktop */

    /* State (engine writes, game reads) */
    uint32_t width;     /* Canvas width (pixels) */
    uint32_t height;    /* Canvas height (pixels) */
    uint32_t fb_width;  /* Actual framebuffer width */
    uint32_t fb_height; /* Actual framebuffer height */
    float dpr;          /* Current effective Device Pixel Ratio */

    /* Internal platform context (used by backends) */
    void *platform_handle;
} nt_window_t;

extern nt_window_t g_nt_window;

/* ---- Lifecycle (platform-specific) ---- */

void nt_window_init(void);
void nt_window_poll(void);
void nt_window_shutdown(void);
void nt_window_set_fullscreen(bool fullscreen);

/* ---- Presentation ---- */

void nt_window_swap_buffers(void);
void nt_window_set_vsync(nt_vsync_t mode);

/* ---- Pre-swap hooks ---- */

/* A hook run by nt_window_swap_buffers JUST BEFORE the platform buffer swap, while the freshly-rendered
   back buffer is still valid (the GL-valid seam the devapi capture group needs). A system registers once
   at startup; the host then just renders + swaps as usual — there is no per-frame hook call to forget. */
typedef void (*nt_window_pre_swap_hook_fn)(void);
void nt_window_add_pre_swap_hook(nt_window_pre_swap_hook_fn fn); /* idempotent for the same fn. */
void nt_window_run_pre_swap_hooks(void);                         /* invoked by each backend's swap_buffers. */

/* ---- Close management ---- */

bool nt_window_should_close(void);
void nt_window_request_close(void);

/* ---- Shared DPR math (used by all platform backends) ---- */

void nt_window_apply_sizes(float canvas_w, float canvas_h, float device_dpr);

#endif /* NT_WINDOW_H */
