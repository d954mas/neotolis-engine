#ifndef ATLAS_BENCH_CLI_H
#define ATLAS_BENCH_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nt_atlas_format.h" /* NT_ATLAS_XFORM_* stored transform value indices */

#define ATLAS_BENCH_MAX_ATLAS_SIZE 16384U

/* D4 transform-mask presets mirrored for the standalone CLI; bit i permits
 * stored transform VALUE i. main.c static-asserts these equal the builder's
 * NT_ATLAS_TRANSFORMS_* so the two sources cannot drift. */
#define ATLAS_BENCH_TRANSFORMS_ALL ((uint8_t)0xFFU)
#define ATLAS_BENCH_TRANSFORMS_IDENTITY ((uint8_t)(1U << NT_ATLAS_XFORM_IDENTITY))
#define ATLAS_BENCH_TRANSFORMS_IDENTITY_ROT90 ((uint8_t)(ATLAS_BENCH_TRANSFORMS_IDENTITY | (1U << NT_ATLAS_XFORM_ROT90)))
#define ATLAS_BENCH_TRANSFORMS_ROTATIONS ((uint8_t)(ATLAS_BENCH_TRANSFORMS_IDENTITY | (1U << NT_ATLAS_XFORM_ROT90) | (1U << NT_ATLAS_XFORM_ROT180) | (1U << NT_ATLAS_XFORM_ROT270)))
#define ATLAS_BENCH_TRANSFORMS_FLIPS ((uint8_t)(ATLAS_BENCH_TRANSFORMS_IDENTITY | (1U << NT_ATLAS_XFORM_FLIP_H) | (1U << NT_ATLAS_XFORM_FLIP_V) | (1U << NT_ATLAS_XFORM_ROT180)))

bool atlas_bench_parse_u32_strict(const char *text, uint32_t *out_value);
bool atlas_bench_parse_max_size(const char *text, uint32_t *out_value);
/* Parse a hex byte or case-insensitive all/identity/identity-rot90/rotations/flips. */
bool atlas_bench_parse_transforms(const char *text, uint8_t *out_mask);
bool atlas_bench_derive_pack_paths(const char *out_json, char *selected, size_t selected_size, char *baseline, size_t baseline_size);
void atlas_bench_cleanup_outputs(const char *out_json, const char *baseline_pack_path, const char *selected_pack_path, bool keep_selected, bool remove_json);

#endif
