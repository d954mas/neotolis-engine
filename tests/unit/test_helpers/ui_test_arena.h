#ifndef NT_TEST_HELPER_UI_TEST_ARENA_H
#define NT_TEST_HELPER_UI_TEST_ARENA_H

/* Comfortable static-array size for nt_ui ctx arena in tests. Public API
 * uses nt_ui_min_arena_size() for exact runtime sizing.
 * 2 MB headroom covers Phase 3 mat4 baked extension (80B/elem × 2 arrays) + Clay. */
#define NT_UI_TEST_ARENA_SIZE (2U * 1024U * 1024U)

#endif /* NT_TEST_HELPER_UI_TEST_ARENA_H */
