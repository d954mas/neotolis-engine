#ifndef ATLAS_BENCH_CLI_H
#define ATLAS_BENCH_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATLAS_BENCH_MAX_ATLAS_SIZE 16384U

bool atlas_bench_parse_u32_strict(const char *text, uint32_t *out_value);
bool atlas_bench_parse_max_size(const char *text, uint32_t *out_value);
bool atlas_bench_derive_pack_paths(const char *out_json, char *selected, size_t selected_size, char *baseline, size_t baseline_size);
void atlas_bench_cleanup_outputs(const char *out_json, const char *baseline_pack_path, const char *selected_pack_path, bool keep_selected, bool remove_json);

#endif
