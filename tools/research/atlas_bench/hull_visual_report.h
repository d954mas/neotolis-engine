#ifndef NT_HULL_VISUAL_REPORT_H
#define NT_HULL_VISUAL_REPORT_H

int nt_hull_visual_generate(const char *corpus_path, const char *frontier_path, const char *out_dir);
int nt_hull_visual_validate(const char *manifest_path, const char *html_path, const char *required_samples);

#endif /* NT_HULL_VISUAL_REPORT_H */
