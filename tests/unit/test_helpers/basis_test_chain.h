#ifndef BASIS_TEST_CHAIN_H
#define BASIS_TEST_CHAIN_H

/* RGBA8 level chains owned by the tests, so encoder pins never follow the
 * builder's filtering. */
#include "nt_texture_format.h"
#include "unity.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BASIS_TEST_MAX_LEVELS 16U

typedef struct {
    uint8_t *data;
    const uint8_t *levels[BASIS_TEST_MAX_LEVELS];
    uint32_t count;
} basis_test_chain_t;

/* Level 0 is `rgba`; level l takes texel (2x, 2y) of level l-1, clamped. */
static inline basis_test_chain_t basis_test_chain(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t count) {
    TEST_ASSERT_TRUE(count >= 1 && count <= BASIS_TEST_MAX_LEVELS);
    size_t total = 0;
    for (uint32_t l = 0; l < count; l++) {
        total += (size_t)nt_texture_level_extent(width, l) * nt_texture_level_extent(height, l) * 4U;
    }
    basis_test_chain_t chain = {.data = (uint8_t *)malloc(total), .count = count};
    TEST_ASSERT_NOT_NULL(chain.data);
    memcpy(chain.data, rgba, (size_t)width * height * 4U);
    uint8_t *dst = chain.data;
    for (uint32_t l = 0; l < count; l++) {
        chain.levels[l] = dst;
        const uint32_t w = nt_texture_level_extent(width, l);
        const uint32_t h = nt_texture_level_extent(height, l);
        if (l > 0) {
            const uint8_t *prev = chain.levels[l - 1];
            const uint32_t pw = nt_texture_level_extent(width, l - 1);
            const uint32_t ph = nt_texture_level_extent(height, l - 1);
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    const uint32_t sx = 2 * x < pw ? 2 * x : pw - 1;
                    const uint32_t sy = 2 * y < ph ? 2 * y : ph - 1;
                    memcpy(&dst[(((size_t)y * w) + x) * 4], &prev[(((size_t)sy * pw) + sx) * 4], 4);
                }
            }
        }
        dst += (size_t)w * h * 4U;
    }
    return chain;
}

static inline void basis_test_chain_free(basis_test_chain_t *chain) {
    free(chain->data);
    chain->data = NULL;
}

#endif /* BASIS_TEST_CHAIN_H */
