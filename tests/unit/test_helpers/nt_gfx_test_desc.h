#ifndef NT_GFX_TEST_DESC_H
#define NT_GFX_TEST_DESC_H

#include "graphics/nt_gfx.h"

#define NT_GFX_TEST_SECOND_STREAM (NT_GFX_FRAME_VERTEX + 1)
_Static_assert(NT_GFX_MAX_VERTEX_STREAMS >= 4, "unit tests use up to four frame vertex streams");

/* Test gfx desc: the fields every test needs, plus the test's own pool sizes. A second vertex
 * stream lets renderer tests route instances away from the general one. */
#define NT_GFX_TEST_DESC(...)                                                                                                                                                                          \
    ((nt_gfx_desc_t){.stream_capacity = 64U * 1024U,                                                                                                                                                   \
                     .frame_capacity = {[NT_GFX_FRAME_VERTEX] = 64U * 1024U, [NT_GFX_TEST_SECOND_STREAM] = 16U * 1024U, [NT_GFX_FRAME_INDEX] = 16U * 1024U, [NT_GFX_FRAME_UNIFORM] = 16U * 1024U},     \
                     __VA_ARGS__})

#endif /* NT_GFX_TEST_DESC_H */
