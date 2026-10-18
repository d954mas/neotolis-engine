#ifndef NT_CORE_H
#define NT_CORE_H

#include "core/nt_types.h"

#define NT_VERSION_MAJOR 0
#define NT_VERSION_MINOR 1
#define NT_VERSION_PATCH 0
#define NT_VERSION_STR_(x) #x
#define NT_VERSION_STR(x) NT_VERSION_STR_(x)
/* "0.1.0". The commit-qualified version is nt_version_engine() (engine/version). */
#define NT_VERSION_SEMVER NT_VERSION_STR(NT_VERSION_MAJOR) "." NT_VERSION_STR(NT_VERSION_MINOR) "." NT_VERSION_STR(NT_VERSION_PATCH)

typedef struct {
    const char *app_name;
    uint32_t version;
} nt_engine_config_t;

nt_result_t nt_engine_init(const nt_engine_config_t *config);
void nt_engine_shutdown(void);

/* Build identity (CMake-injected). Engine-wide facts, not just for tooling. */
const char *nt_engine_build_string(void);  /* CMAKE_BUILD_TYPE, e.g. "Debug" */
const char *nt_engine_preset_string(void); /* active CMake preset, e.g. "native-debug" */

#endif // NT_CORE_H
