#ifndef NT_CORE_H
#define NT_CORE_H

#include "core/nt_types.h"

#define NT_VERSION_MAJOR 0
#define NT_VERSION_MINOR 1
#define NT_VERSION_PATCH 0

typedef struct {
    const char *app_name;
    uint32_t version;
} nt_engine_config_t;

nt_result_t nt_engine_init(const nt_engine_config_t *config);
void nt_engine_shutdown(void);
const char *nt_engine_version_string(void);

#endif // NT_CORE_H
