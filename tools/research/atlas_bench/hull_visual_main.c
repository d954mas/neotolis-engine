#include "hull_visual_report.h"

#include <stdio.h>
#include <string.h>

static const char *arg_value(int argc, char **argv, const char *name) {
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: atlas_hull_visual_report generate|validate [options]\n");
        return 2;
    }
    if (strcmp(argv[1], "generate") == 0) {
        const char *frontier = arg_value(argc, argv, "--frontier");
        const char *out = arg_value(argc, argv, "--out");
        return nt_hull_visual_generate(frontier, out);
    }
    if (strcmp(argv[1], "validate") == 0) {
        const char *manifest = arg_value(argc, argv, "--manifest");
        const char *html = arg_value(argc, argv, "--html");
        const char *required = arg_value(argc, argv, "--require-samples");
        return nt_hull_visual_validate(manifest, html, required);
    }
    (void)fprintf(stderr, "atlas_hull_visual_report: unknown mode '%s'\n", argv[1]);
    return 2;
}
