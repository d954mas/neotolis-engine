#ifndef NT_HULL_VISUAL_REPORT_H
#define NT_HULL_VISUAL_REPORT_H

#include <stdint.h>

int nt_hull_visual_generate(const char *corpus_path, const char *frontier_path, const char *out_dir);
int nt_hull_visual_validate(const char *manifest_path, const char *html_path, const char *required_samples);

/* Test seam for fail-closed ownership paths in the offline report generator. */
void nt_hull_visual_test_fail_after_stage(uint32_t stage);
uint32_t nt_hull_visual_test_live_buffers(void);

#endif /* NT_HULL_VISUAL_REPORT_H */
