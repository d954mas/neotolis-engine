#ifndef NT_GFX_INTERNAL_H
#define NT_GFX_INTERNAL_H

#include "graphics/nt_gfx.h"

/* ---- Handle encoding ---- */

#define NT_GFX_SLOT_SHIFT 16
#define NT_GFX_SLOT_MASK 0xFFFF

/* ---- Pool slot ---- */

typedef struct {
    uint32_t id; /* matches handle.id when valid, 0 when free */
} nt_gfx_slot_t;

/* ---- Resource pool ---- */

typedef struct {
    nt_gfx_slot_t *slots;
    uint32_t capacity;
    uint32_t *free_queue; /* stack of free slot indices */
    uint32_t queue_top;   /* next free position (stack pointer) */
} nt_gfx_pool_t;

/* ---- Render state machine ---- */

typedef enum {
    NT_GFX_STATE_IDLE = 0,
    NT_GFX_STATE_FRAME,
    NT_GFX_STATE_PASS,
} nt_gfx_render_state_t;

/* ---- Pool helper functions (implemented in nt_gfx.c) ---- */

void nt_gfx_pool_init(nt_gfx_pool_t *pool, uint32_t capacity);
void nt_gfx_pool_shutdown(nt_gfx_pool_t *pool);
uint32_t nt_gfx_pool_alloc(nt_gfx_pool_t *pool);
void nt_gfx_pool_free(nt_gfx_pool_t *pool, uint32_t id);
bool nt_gfx_pool_valid(const nt_gfx_pool_t *pool, uint32_t id);
uint32_t nt_gfx_pool_slot_index(uint32_t id);

/* ---- Backend function signatures (implemented by each backend) ---- */

void nt_gfx_backend_init(const nt_gfx_desc_t *desc);
void nt_gfx_backend_shutdown(void);
bool nt_gfx_backend_is_context_lost(void);

void nt_gfx_backend_begin_frame(void);
void nt_gfx_backend_end_frame(void);
void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc);
void nt_gfx_backend_end_pass(void);

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc);
void nt_gfx_backend_destroy_shader(uint32_t backend_handle);

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t vs_backend, uint32_t fs_backend);
void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle);

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc);
void nt_gfx_backend_destroy_buffer(uint32_t backend_handle);
void nt_gfx_backend_update_buffer(uint32_t backend_handle, const void *data, uint32_t size);

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle);
void nt_gfx_backend_bind_vertex_buffer(uint32_t backend_handle);
void nt_gfx_backend_bind_index_buffer(uint32_t backend_handle);

void nt_gfx_backend_set_uniform_mat4(const char *name, const float *matrix);
void nt_gfx_backend_set_uniform_vec4(const char *name, const float *vec);
void nt_gfx_backend_set_uniform_float(const char *name, float val);
void nt_gfx_backend_set_uniform_int(const char *name, int val);

void nt_gfx_backend_draw(uint32_t first_element, uint32_t num_elements, bool indexed);

void nt_gfx_backend_recreate_all_resources(void);

#endif /* NT_GFX_INTERNAL_H */
