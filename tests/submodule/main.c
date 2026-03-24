/* Submodule consumption test — verifies all engine modules compile and link
 * when the engine is used as a CMake subdirectory (not the top-level project). */

#include "core/nt_core.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "pool/nt_pool.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"

int main(void) {
    nt_engine_config_t config = {.app_name = "submodule_test", .version = 1};
    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }

    /* Verify key modules are callable */
    (void)nt_time_now();
    (void)nt_hash64_str("test");

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);

    nt_resource_desc_t res_desc = {0};
    nt_resource_init(&res_desc);

    nt_resource_shutdown();
    nt_gfx_shutdown();

    nt_log_info("submodule consumption OK — all modules linked");
    nt_engine_shutdown();
    return 0;
}
