#ifndef NT_TEXT_RENDERER_H
#define NT_TEXT_RENDERER_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

/* ---- Compile-time limits (per D-14) ---- */

#ifndef NT_TEXT_RENDERER_MAX_GLYPHS
#define NT_TEXT_RENDERER_MAX_GLYPHS 4096
#endif

#define NT_TEXT_RENDERER_MAX_VERTICES (NT_TEXT_RENDERER_MAX_GLYPHS * 4)
#define NT_TEXT_RENDERER_MAX_INDICES (NT_TEXT_RENDERER_MAX_GLYPHS * 6)

/* ---- Lifecycle ---- */

void nt_text_renderer_init(void);
void nt_text_renderer_shutdown(void);
void nt_text_renderer_restore_gpu(void);

/* ---- State setters (auto-flush on change per D-18, D-19, D-20) ---- */

void nt_text_renderer_set_material(nt_material_t mat);
void nt_text_renderer_set_font(nt_font_t font);

/* ---- Draw (adds text to CPU staging buffer, per-draw mat4 pre-transform) ---- */

void nt_text_renderer_draw(const char *utf8, const float model[16], float size, const float color[4]);

/* ---- Flush (upload staging buffer + single draw call per flush) ---- */

void nt_text_renderer_flush(void);

/* ---- Test accessors (test builds only) ---- */

#ifdef NT_TEXT_RENDERER_TEST_ACCESS
uint32_t nt_text_renderer_test_vertex_count(void);
uint32_t nt_text_renderer_test_glyph_count(void);
const void *nt_text_renderer_test_vertices(void);
bool nt_text_renderer_test_initialized(void);
#endif

#endif /* NT_TEXT_RENDERER_H */
