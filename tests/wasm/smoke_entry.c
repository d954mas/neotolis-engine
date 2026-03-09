/**
 * WASM smoke test entry point.
 *
 * Minimal program that initializes the engine, verifies success,
 * shuts down, and exits. Built with -sMODULARIZE for Node.js loading.
 *
 * Build: emcmake cmake --preset wasm-debug && cmake --build --preset wasm-debug
 * Run:   node tests/wasm/smoke_test.js
 */
#include "core/nt_core.h"
#include <stdio.h>

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "wasm_smoke";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("WASM smoke test FAILED: nt_engine_init returned %d\n", result);
        return 1;
    }

    printf("WASM smoke test: engine init OK\n");

    nt_engine_shutdown();
    printf("WASM smoke test: shutdown OK\n");
    return 0;
}
