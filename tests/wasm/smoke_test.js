/**
 * WASM smoke test -- Node.js script.
 *
 * Loads the Emscripten-generated WASM module (built with -sMODULARIZE)
 * and verifies that engine init/shutdown succeeds.
 *
 * Usage: node tests/wasm/smoke_test.js [path/to/wasm_smoke.js]
 */
const path = require('path');

async function main() {
    const wasmPath = process.argv[2]
        ? path.resolve(process.argv[2])
        : path.resolve(__dirname, '../../build/tests/wasm-debug/wasm_smoke.js');

    let createModule;
    try {
        createModule = require(wasmPath);
    } catch (err) {
        console.error('WASM smoke test FAILED: cannot load module at', wasmPath);
        console.error(err.message);
        process.exit(1);
    }

    try {
        await createModule();
        // If main() ran and returned 0, Emscripten resolves the promise.
        console.log('WASM smoke test PASSED');
        process.exit(0);
    } catch (err) {
        // Emscripten may throw an "exit" error with status 0 when main() returns.
        // That counts as success.
        if (err.status === 0 || (err.message && err.message.includes('exit(0)'))) {
            console.log('WASM smoke test PASSED');
            process.exit(0);
        }
        console.error('WASM smoke test FAILED:', err.message || err);
        process.exit(1);
    }
}

main();
