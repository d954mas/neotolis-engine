#ifndef NT_HULL_VISUAL_REPORT_H
#define NT_HULL_VISUAL_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int nt_hull_visual_generate(const char *frontier_path, const char *out_dir);
int nt_hull_visual_validate(const char *manifest_path, const char *html_path, const char *required_samples);

/* Test seams expose fail-closed report paths without weakening production checks. */
void nt_hull_visual_test_fail_after_stage(uint32_t stage);
uint32_t nt_hull_visual_test_live_buffers(void);
bool nt_hull_visual_test_panel_pack_paths(const char *out_dir, uint32_t ordinal, char *baseline, size_t baseline_size, char *selected, size_t selected_size);

#endif /* NT_HULL_VISUAL_REPORT_H */
