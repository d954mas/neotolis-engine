const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { ntSupportsWasmSimd } = require('../../engine/platform/web/simd_loader.js');

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
    // null wasmApi falls back to global WebAssembly; Node.js supports SIMD
    assert.strictEqual(ntSupportsWasmSimd(null), true);

    const browser = { WebAssembly: undefined };
    vm.runInNewContext(fs.readFileSync(require.resolve('../../engine/platform/web/simd_loader.js'), 'utf8'), browser);
    assert.strictEqual(browser.ntSupportsWasmSimd(), false);
    browser.WebAssembly = WebAssembly;
    assert.strictEqual(browser.ntSupportsWasmSimd(), true);

    // Verify the probe bytes are valid WASM when SIMD is supported.
    // Node.js 16+ supports WASM SIMD, so this catches broken probe bytes in CI.
    if (typeof WebAssembly !== 'undefined' && typeof WebAssembly.validate === 'function') {
        assert.strictEqual(ntSupportsWasmSimd(WebAssembly), true,
            'SIMD probe must pass WebAssembly.validate() in Node.js (which supports SIMD)');
    }

    console.log('SIMD loader test PASSED');
}

run();
