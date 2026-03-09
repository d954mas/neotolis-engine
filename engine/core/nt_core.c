#include "core/nt_core.h"
#include <stdio.h>

static bool s_initialized = false;

nt_result_t nt_engine_init(const nt_engine_config_t *config) {
    if (s_initialized) {
        printf("Engine already initialized\n");
        return NT_ERR_INIT_FAILED;
    }

    if (config == NULL) {
        printf("Invalid engine config\n");
        return NT_ERR_INVALID_ARG;
    }

    printf("Neotolis Engine %s initializing: app='%s'\n", nt_engine_version_string(), config->app_name ? config->app_name : "(unnamed)");

    s_initialized = true;
    return NT_OK;
}

void nt_engine_shutdown(void) {
    if (!s_initialized) {
        return;
    }

    printf("Neotolis Engine shutting down\n");
    s_initialized = false;
}

const char *nt_engine_version_string(void) {
    static char version_buf[32] = {0};
    if (version_buf[0] == '\0') {
        (void)snprintf(version_buf, sizeof(version_buf), "%d.%d.%d", NT_VERSION_MAJOR, NT_VERSION_MINOR, NT_VERSION_PATCH);
    }
    return version_buf;
}
