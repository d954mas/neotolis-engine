(function(root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
        return;
    }

    var api = factory();
    root.ntSupportsWasmSimd = api.ntSupportsWasmSimd;
})(typeof globalThis !== 'undefined' ? globalThis : this, function() {
    // Minimal WASM module: (func (drop (i8x16.splat (i32.const 0))))
    // Validation succeeds only when WebAssembly SIMD is supported.
    var SIMD_PROBE = new Uint8Array([
        0, 97, 115, 109, 1, 0, 0, 0,
        1, 4, 1, 96, 0, 0,
        3, 2, 1, 0,
        10, 9, 1, 7, 0, 65, 0, 253, 15, 26, 11
    ]);

    function ntSupportsWasmSimd(wasmApi) {
        var webAssemblyApi = wasmApi;
        if (!webAssemblyApi && typeof WebAssembly !== 'undefined') {
            webAssemblyApi = WebAssembly;
        }

        if (!webAssemblyApi || typeof webAssemblyApi.validate !== 'function') {
            return false;
        }

        try {
            return webAssemblyApi.validate(SIMD_PROBE);
        } catch (e) {
            return false;
        }
    }

    return {
        ntSupportsWasmSimd: ntSupportsWasmSimd
    };
});
