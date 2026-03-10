#include "core/nt_core.h"
#include "platform/web/nt_web.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "hello";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("Failed to initialize engine: error %d\n", result);
        return 1;
    }

    nt_web_loading_complete();

    printf("Hello from Neotolis Engine!\n");
    printf("Canvas present but no rendering in Phase 1\n");

    nt_engine_shutdown();
    return 0;
}
