#include "app/nt_app.h"

/* Single definition of global frame state -- shared by all platform backends.
   Static storage: dt, time, frame are zero-initialized by C standard. */
nt_app_t g_nt_app = {.max_dt = 0.1F, .vsync = NT_VSYNC_ON};
