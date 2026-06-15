#include "core/nt_core.h"
#include "log/nt_log.h"
#include <stdio.h>

/* Build identity injected by CMake (engine/core/CMakeLists.txt). "unknown" keeps
   core compilable when the build facts aren't passed (e.g. bare unit tests). */
#ifndef NT_BUILD_TYPE
#define NT_BUILD_TYPE "unknown"
#endif
#ifndef NT_PRESET_NAME
#define NT_PRESET_NAME "unknown"
#endif

static bool s_initialized = false;

nt_result_t nt_engine_init(const nt_engine_config_t *config) {
    if (s_initialized) {
        NT_LOG_ERROR("engine already initialized");
        return NT_ERR_INIT_FAILED;
    }

    if (config == NULL) {
        NT_LOG_ERROR("invalid engine config");
        return NT_ERR_INVALID_ARG;
    }

    NT_LOG_INFO("Neotolis Engine %s initializing: app='%s'", nt_engine_version_string(), config->app_name ? config->app_name : "(unnamed)");

    s_initialized = true;
    return NT_OK;
}

void nt_engine_shutdown(void) {
    if (!s_initialized) {
        return;
    }

    NT_LOG_INFO("Neotolis Engine shutting down");
    s_initialized = false;
}

const char *nt_engine_version_string(void) {
    static char version_buf[32] = {0};
    if (version_buf[0] == '\0') {
        (void)snprintf(version_buf, sizeof(version_buf), "%d.%d.%d", NT_VERSION_MAJOR, NT_VERSION_MINOR, NT_VERSION_PATCH);
    }
    return version_buf;
}

const char *nt_engine_build_string(void) { return NT_BUILD_TYPE; }

const char *nt_engine_preset_string(void) { return NT_PRESET_NAME; }
