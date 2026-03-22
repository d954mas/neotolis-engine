const assert = require('assert');
const { ntResolveWasmLoad, ntResolveWasmPath, ntSupportsWasmSimd } = require('../../engine/platform/web/simd_loader.js');

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

    assert.deepStrictEqual(ntResolveWasmLoad('index.wasm', 'index_simd.wasm', wasmOk), {
        path: 'index_simd.wasm',
        variant: 'simd'
    });
    assert.deepStrictEqual(ntResolveWasmLoad('index.wasm', 'index_simd.wasm', wasmNo), {
        path: 'index.wasm',
        variant: 'baseline'
    });
    assert.deepStrictEqual(ntResolveWasmLoad('index.wasm', '', wasmOk), {
        path: 'index.wasm',
        variant: 'baseline'
    });

    console.log('SIMD loader test PASSED');
}

run();
