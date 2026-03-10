#include "core/nt_core.h"
#include "log/nt_log.h"

int main(void) {
    nt_engine_config_t config = {.app_name = "submodule_test", .version = 1};
    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }
    nt_log_info("submodule consumption OK");
    nt_engine_shutdown();
    return 0;
}
