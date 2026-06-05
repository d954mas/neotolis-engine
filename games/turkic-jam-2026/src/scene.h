#ifndef TJ_SCENE_H
#define TJ_SCENE_H

/* Lightweight scene contract. Transition is requested via game_goto and applied
 * at the next frame boundary (never mid-build of the UI tree). */

typedef struct game_ctx game_ctx_t;

typedef struct scene {
    const char *name;
    void (*on_enter)(game_ctx_t *g);
    void (*on_update)(game_ctx_t *g, float dt); /* build UI + per-frame logic */
    void (*on_exit)(game_ctx_t *g);
} scene_t;

#endif /* TJ_SCENE_H */
