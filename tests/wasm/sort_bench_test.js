/**
 * WASM sort benchmark -- Node.js runner.
 *
 * Loads the Emscripten-generated sort benchmark module and captures output.
 *
 * Usage:
 *   node tests/wasm/sort_bench_test.js --module /path/to/wasm_sort_bench.js
 */
const path = require('path');

async function main() {
    const args = process.argv.slice(2);
    const moduleFlagIndex = args.indexOf('--module');
    if (moduleFlagIndex < 0 || !args[moduleFlagIndex + 1]) {
        console.error('Usage: node sort_bench_test.js --module <path-to-wasm-module.js>');
        process.exit(1);
    }

    const wasmPath = path.resolve(args[moduleFlagIndex + 1]);
    const label = path.basename(wasmPath);

    let createModule;
    try {
        createModule = require(wasmPath);
    } catch (err) {
        console.error(`Sort benchmark FAILED (${label}): cannot load module at`, wasmPath);
        console.error(err.message);
        process.exit(1);
    }

    try {
        await createModule();
        console.log(`Sort benchmark PASSED (${label})`);
        process.exit(0);
    } catch (err) {
        if (err.status === 0 || (err.message && err.message.includes('exit(0)'))) {
            console.log(`Sort benchmark PASSED (${label})`);
            process.exit(0);
        }
        console.error(`Sort benchmark FAILED (${label}):`, err.message || err);
        process.exit(1);
    }
}

main();
