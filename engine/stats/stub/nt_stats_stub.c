#include "stats/nt_stats.h"

/* Minimal headless stub for tests that link the devapi `render` group (cmd_render_info) but
   want no nt_gfx/nt_font/nt_text_renderer chain. Only the symbol the group references is defined;
   draw_calls is 0 in a no-draw test. Mirrors nt_window_stub. */

uint32_t nt_stats_get_draw_calls(void) { return 0U; }
