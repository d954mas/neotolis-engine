/**
 * WASM smoke test -- Node.js script.
 *
 * Loads the Emscripten-generated WASM module (built with -sMODULARIZE)
 * and verifies that engine init/shutdown succeeds.
 *
 * Usage:
 *   node tests/wasm/smoke_test.js [preset]
 *   node tests/wasm/smoke_test.js --module /absolute/path/to/wasm_smoke.js
 */
const path = require('path');

async function main() {
    const args = process.argv.slice(2);
    const moduleFlagIndex = args.indexOf('--module');
    if (moduleFlagIndex >= 0 && !args[moduleFlagIndex + 1]) {
        console.error('WASM smoke test FAILED: --module requires a path argument');
        process.exit(1);
    }

    const preset = moduleFlagIndex >= 0 ? null : (args[0] || 'wasm-debug');
    const wasmPath = moduleFlagIndex >= 0
        ? path.resolve(args[moduleFlagIndex + 1])
        : path.resolve(__dirname, `../../build/tests/${preset}/wasm_smoke.js`);
    const label = preset || path.basename(wasmPath);

    let createModule;
    try {
        createModule = require(wasmPath);
    } catch (err) {
        console.error(`WASM smoke test FAILED (${label}): cannot load module at`, wasmPath);
        console.error(err.message);
        process.exit(1);
    }

    try {
        await createModule();
        // If main() ran and returned 0, Emscripten resolves the promise.
        console.log(`WASM smoke test PASSED (${label})`);
        process.exit(0);
    } catch (err) {
        // Emscripten may throw an "exit" error with status 0 when main() returns.
        // That counts as success.
        if (err.status === 0 || (err.message && err.message.includes('exit(0)'))) {
            console.log(`WASM smoke test PASSED (${label})`);
            process.exit(0);
        }
        console.error(`WASM smoke test FAILED (${label}):`, err.message || err);
        process.exit(1);
    }
}

main();
