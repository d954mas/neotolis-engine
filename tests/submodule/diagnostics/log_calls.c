#define NT_LOG_DOMAIN "nt426.domain"
#include "log/nt_log.h"

extern int diagnostics_argument(void);

bool diagnostics_log_calls(void) {
    bool emitted = false;
    nt_log_info("NT426_INFO_PLAIN %d", diagnostics_argument());
    nt_log_info_once("NT426_INFO_ONCE %d", diagnostics_argument());
    emitted |= nt_log_info_unique("NT426_INFO_UNIQUE %d", diagnostics_argument());
    NT_LOG_INFO("NT426_INFO_DOMAIN_PLAIN %d", diagnostics_argument());
    NT_LOG_INFO_ONCE("NT426_INFO_DOMAIN_ONCE %d", diagnostics_argument());
    emitted |= NT_LOG_INFO_UNIQUE("NT426_INFO_DOMAIN_UNIQUE %d", diagnostics_argument());
    nt_log_warn("NT426_WARN_PLAIN %d", diagnostics_argument());
    nt_log_warn_once("NT426_WARN_ONCE %d", diagnostics_argument());
    emitted |= nt_log_warn_unique("NT426_WARN_UNIQUE %d", diagnostics_argument());
    NT_LOG_WARN("NT426_WARN_DOMAIN_PLAIN %d", diagnostics_argument());
    NT_LOG_WARN_ONCE("NT426_WARN_DOMAIN_ONCE %d", diagnostics_argument());
    emitted |= NT_LOG_WARN_UNIQUE("NT426_WARN_DOMAIN_UNIQUE %d", diagnostics_argument());
    nt_log_error("NT426_ERROR_PLAIN %d", diagnostics_argument());
    nt_log_error_once("NT426_ERROR_ONCE %d", diagnostics_argument());
    emitted |= nt_log_error_unique("NT426_ERROR_UNIQUE %d", diagnostics_argument());
    NT_LOG_ERROR("NT426_ERROR_DOMAIN_PLAIN %d", diagnostics_argument());
    NT_LOG_ERROR_ONCE("NT426_ERROR_DOMAIN_ONCE %d", diagnostics_argument());
    emitted |= NT_LOG_ERROR_UNIQUE("NT426_ERROR_DOMAIN_UNIQUE %d", diagnostics_argument());
    return emitted;
}
