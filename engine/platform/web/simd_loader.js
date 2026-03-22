(function(root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
        return;
    }

    var api = factory();
    root.ntSupportsWasmSimd = api.ntSupportsWasmSimd;
    root.ntResolveWasmPath = api.ntResolveWasmPath;
    root.ntResolveWasmLoad = api.ntResolveWasmLoad;
})(typeof globalThis !== 'undefined' ? globalThis : this, function() {
    // Minimal WASM module: (func (drop (i8x16.splat (i32.const 0))))
    // Validation succeeds only when WebAssembly SIMD is supported.
    var SIMD_PROBE = new Uint8Array([
        0, 97, 115, 109, 1, 0, 0, 0,
        1, 4, 1, 96, 0, 0,
        3, 2, 1, 0,
        10, 9, 1, 7, 0, 65, 0, 253, 15, 11
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

    function ntResolveWasmPath(path, simdWasmPath, wasmApi) {
        if (!simdWasmPath) {
            return path;
        }

        if (typeof path !== 'string' || !path.endsWith('.wasm')) {
            return path;
        }

        if (!ntSupportsWasmSimd(wasmApi)) {
            return path;
        }

        return simdWasmPath;
    }

    function ntResolveWasmLoad(path, simdWasmPath, wasmApi) {
        var resolvedPath = ntResolveWasmPath(path, simdWasmPath, wasmApi);
        return {
            path: resolvedPath,
            variant: resolvedPath === simdWasmPath ? 'simd' : 'baseline'
        };
    }

    return {
        ntSupportsWasmSimd: ntSupportsWasmSimd,
        ntResolveWasmPath: ntResolveWasmPath,
        ntResolveWasmLoad: ntResolveWasmLoad
    };
});
