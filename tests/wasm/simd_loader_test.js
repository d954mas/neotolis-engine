const assert = require('assert');
const { ntResolveWasmPath, ntSupportsWasmSimd } = require('../../engine/platform/web/simd_loader.js');

function run() {
    const wasmOk = {
        validate(bytes) {
            assert(bytes instanceof Uint8Array);
            return true;
        }
    };
    const wasmNo = {
        validate() {
            return false;
        }
    };
    const wasmThrows = {
        validate() {
            throw new Error('nope');
        }
    };

    assert.strictEqual(ntSupportsWasmSimd(wasmOk), true);
    assert.strictEqual(ntSupportsWasmSimd(wasmNo), false);
    assert.strictEqual(ntSupportsWasmSimd(wasmThrows), false);
    assert.strictEqual(ntSupportsWasmSimd({}), false);
    assert.strictEqual(ntSupportsWasmSimd(null), false);

    assert.strictEqual(ntResolveWasmPath('index.wasm', 'index_simd.wasm', wasmOk), 'index_simd.wasm');
    assert.strictEqual(ntResolveWasmPath('index.wasm', 'index_simd.wasm', wasmNo), 'index.wasm');
    assert.strictEqual(ntResolveWasmPath('index.js', 'index_simd.wasm', wasmOk), 'index.js');
    assert.strictEqual(ntResolveWasmPath('index.wasm', '', wasmOk), 'index.wasm');

    console.log('SIMD loader test PASSED');
}

run();
