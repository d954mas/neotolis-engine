#ifndef NT_APP_H
#define NT_APP_H

#include "core/nt_types.h"

typedef void (*nt_app_frame_fn)(void);

/* ---- Time-control mode (managed loop only; RUN = 0 so zero-init = RUN) ---- */

typedef enum nt_app_mode_t {
    NT_APP_MODE_RUN = 0, /* dt = clamped wall dt * scale */
    NT_APP_MODE_MANUAL   /* dt = step_dt only on a pending step, else 0 (lockstep) */
} nt_app_mode_t;

/* ---- Frame state (engine writes, game reads) ---- */

typedef struct nt_app_t {
    float dt;        /* Clamped delta time (seconds) */
    double time;     /* Elapsed seconds; double so long deterministic runs don't lose resolution to float accumulation */
    float max_dt;    /* Clamp threshold (seconds), default 0.1f */
    float target_dt; /* Frame rate cap (seconds), 0 = uncapped */
    uint32_t frame;  /* Sim-advance counter (frozen on pause/manual-idle); uint32 wrap is intentional — devapi deadlines use modular compare, bounded < 2^31 */

    /* Managed-loop time control (read/written via the nt_app_* mutators below). */
    bool paused;         /* RUN-mode pause: dt = 0, frame frozen */
    float scale;         /* RUN-mode dt multiplier (observation only), default 1.0f */
    nt_app_mode_t mode;  /* RUN or MANUAL */
    float step_dt;       /* MANUAL fixed dt fed per step, default 1/60 */
    int pending_steps;   /* MANUAL queued steps (consumed one per advance) */
    bool render_enabled; /* Loop-agnostic draw-pass gate, default true */
} nt_app_t;

extern nt_app_t g_nt_app;

/* ---- Frame loop API ---- */

/* Single frame loop owning the g_nt_app.dt scalar. Default (mode RUN, scale 1, unpaused) is a
   plain wall-clock advance; the time-control mutators below switch it to pause / manual lockstep /
   scaled time. The game keeps wiring poll/input/update/render inside fn. */
void nt_app_run(nt_app_frame_fn fn);
void nt_app_quit(void);

/* ---- Time control (applied by nt_app_run; default state is a plain wall-clock loop) ---- */

void nt_app_pause(void); /* RUN: dt -> 0, frame frozen, fn still runs */
void nt_app_resume(void);
void nt_app_set_scale(float scale); /* RUN dt multiplier (NOT a determinism primitive) */
void nt_app_set_mode(nt_app_mode_t mode);
void nt_app_set_step_dt(float step_dt);
void nt_app_step(int count); /* MANUAL: queue `count` fixed-dt advances */

void nt_app_set_render_enabled(bool enabled);
bool nt_app_render_enabled(void);

#endif /* NT_APP_H */
