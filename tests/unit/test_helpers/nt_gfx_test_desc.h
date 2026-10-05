#ifndef NT_GFX_TEST_DESC_H
#define NT_GFX_TEST_DESC_H

#include "graphics/nt_gfx.h"

/* Test gfx desc: the fields every test needs, plus the test's own pool sizes. */
#define NT_GFX_TEST_DESC(...) ((nt_gfx_desc_t){.stream_capacity = 64U * 1024U, __VA_ARGS__})

#endif /* NT_GFX_TEST_DESC_H */
