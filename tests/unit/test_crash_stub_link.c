/* A headless executable links crash capture with nt_gfx_stub and never initializes graphics. */
#include "crash/nt_crash.h"
#include "graphics/nt_gfx.h"

#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        (void)fprintf(stderr, "usage: test_crash_stub_link <path>\n");
        return 2;
    }
    nt_crash_init(argv[1], "test_crash_stub_link");
    const nt_gfx_device_info_t *info = nt_gfx_device_info();
    return info->vendor[0] == '\0' && info->renderer[0] == '\0' && info->api_version[0] == '\0' && info->caps[0] == '\0' ? 0 : 1;
}
