#ifndef TJ_GAME_H
#define TJ_GAME_H

/* Shared game context + constants. Passed to every scene; holds the UI context
 * and bound render resources so scenes never touch engine init directly. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "resource/nt_resource.h"

#include "scene.h"

/* Clay userData layer tags (debug-walker layer sort). */
#define TJ_LAYER_IMG 1
#define TJ_LAYER_TEXT 2

/* Logical design resolution (16:9). Content scales to fit any framebuffer. */
#define TJ_REF_W 1280.0F
#define TJ_REF_H 720.0F

typedef struct nt_ui_context nt_ui_context_t;

typedef struct game_ctx {
    nt_ui_context_t *ui;
    nt_font_t font;
    nt_resource_t atlas;
    uint32_t white_region;
    uint32_t btn_blue;
    uint32_t btn_green;
    uint32_t btn_red;
    float logical_w, logical_h;
    bool resources_ready;
    const scene_t *scene;
    const scene_t *next; /* pending transition, applied at frame boundary */
} game_ctx_t;

/* Request a scene change; takes effect at the start of the next frame. */
void game_goto(game_ctx_t *g, const scene_t *next);

extern const scene_t SCENE_MENU;
extern const scene_t SCENE_GAME;

#endif /* TJ_GAME_H */
