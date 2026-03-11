#include "app/nt_app.h"

/* Single definition of global frame state -- shared by all platform backends */
nt_app_t g_nt_app = {.max_dt = 0.1F};
