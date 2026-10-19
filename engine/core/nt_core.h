#ifndef NT_CORE_H
#define NT_CORE_H

#include "core/nt_types.h"

typedef struct {
    const char *app_name;
} nt_engine_config_t;

nt_result_t nt_engine_init(const nt_engine_config_t *config);
void nt_engine_shutdown(void);

/* Build identity (CMake-injected). Engine-wide facts, not just for tooling. */
const char *nt_engine_build_string(void);  /* CMAKE_BUILD_TYPE, e.g. "Debug" */
const char *nt_engine_preset_string(void); /* active CMake preset, e.g. "native-debug" */

#endif // NT_CORE_H
