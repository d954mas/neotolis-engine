#ifndef NT_APP_H
#define NT_APP_H

#include "core/nt_types.h"

/* ---- Callback typedefs ---- */

typedef void (*nt_app_frame_fn)(void);
typedef void (*nt_app_shutdown_fn)(void);

/* ---- Frame state (engine writes, game reads) ---- */

typedef struct nt_app_t {
    float dt;       /* Clamped delta time (seconds) */
    float time;     /* Elapsed time since loop start (seconds) */
    float max_dt;   /* Clamp threshold (seconds), default 0.1f */
    uint32_t frame; /* Frame counter */
} nt_app_t;

extern nt_app_t g_nt_app;

/* ---- Frame loop API ---- */

void nt_app_run(nt_app_frame_fn fn);
void nt_app_quit(void);
void nt_app_on_shutdown(nt_app_shutdown_fn fn);

#endif /* NT_APP_H */
