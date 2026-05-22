#ifndef NT_TEST_HELPER_UI_TEST_ARENA_H
#define NT_TEST_HELPER_UI_TEST_ARENA_H

/* Comfortable static-array size for nt_ui ctx arena in tests. Public API
 * uses nt_ui_min_arena_size() for exact runtime sizing. */
#define NT_UI_TEST_ARENA_SIZE (1U * 1024U * 1024U)

#endif /* NT_TEST_HELPER_UI_TEST_ARENA_H */
