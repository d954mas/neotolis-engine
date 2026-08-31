#include "http/nt_http_internal.h"

#include <emscripten.h>
#include <stdlib.h>

/* ---- EM_JS fetch with AbortController ---- */

/* clang-format off */
/* EM_JS_DEPS keeps these through Closure. malloc (linker-added, no leading _) is kept as a wasm
 * export for wasmExports['malloc']; HEAPU8 is an always-present global (no dep). */
EM_JS_DEPS(nt_http_web, "$UTF8ToString,malloc")

EM_JS(void, nt_http_web_fetch, (int slot_index, int generation, const char *url_ptr, const char *method_ptr,
                                const uint8_t *body_ptr, int body_size, const char *headers_ptr, int headers_size,
                                int timeout_ms), {
    var url = UTF8ToString(url_ptr);

    if (!Module['_nt_http_controllers']) {
        Module['_nt_http_controllers'] = {};
    }
    var controller = new AbortController();
    Module['_nt_http_controllers'][slot_index] = controller;

    var init = { method: UTF8ToString(method_ptr), signal: controller.signal };
    if (headers_size > 0) {
        /* Blob of alternating NUL-terminated name,value strings (trailing NUL -> drop last split part) */
        var parts = new TextDecoder().decode(HEAPU8.subarray(headers_ptr, headers_ptr + headers_size)).split('\0');
        var headers = {};
        for (var i = 0; i + 1 < parts.length; i += 2) {
            headers[parts[i]] = parts[i + 1];
        }
        init.headers = headers;
    }
    if (body_size > 0) {
        init.body = HEAPU8.slice(body_ptr, body_ptr + body_size);
    }

    var timer = 0;
    if (timeout_ms > 0) {
        timer = setTimeout(function() { controller.abort(); }, timeout_ms);
    }

    function finish(ptr, size, status, hdrs_ptr, success) {
        if (timer) clearTimeout(timer);
        delete Module['_nt_http_controllers'][slot_index];
        _nt_http_web_on_complete(slot_index, generation, ptr, size, status, hdrs_ptr, success);
    }
    function headerBlock(response) {
        var s = "";
        response.headers.forEach(function(v, k) { s += k + ': ' + v + '\n'; });
        var bytes = new TextEncoder().encode(s);
        var p = wasmExports['malloc'](bytes.length + 1);
        HEAPU8.set(bytes, p);
        HEAPU8[p + bytes.length] = 0;
        return p;
    }

    fetch(url, init).then(function(response) {
        var status = response.status;
        var hdrs = headerBlock(response);

        /* Try ReadableStream for progress, fall back to arrayBuffer */
        if (response.body && typeof response.body.getReader === 'function') {
            var contentLength = response.headers.get('Content-Length');
            var total = contentLength ? parseInt(contentLength, 10) : 0;
            var received = 0;
            var chunks = [];
            var reader = response.body.getReader();

            function pump() {
                reader.read().then(function(result) {
                    if (result.done) {
                        var totalLen = received;
                        var ptr = wasmExports['malloc'](totalLen);
                        var offset = 0;
                        for (var i = 0; i < chunks.length; i++) {
                            HEAPU8.set(chunks[i], ptr + offset);
                            offset += chunks[i].length;
                        }
                        finish(ptr, totalLen, status, hdrs, 1);
                        return;
                    }
                    var chunk = result.value;
                    chunks.push(chunk);
                    received += chunk.length;
                    _nt_http_web_on_progress(slot_index, generation, received, total);
                    pump();
                }).catch(function(e) {
                    console.error('ERROR [http] stream error slot=' + slot_index, e);
                    finish(0, 0, status, hdrs, 0);
                });
            }
            pump();
        } else {
            /* Fallback: no streaming progress */
            response.arrayBuffer().then(function(buf) {
                var arr = new Uint8Array(buf);
                var ptr = wasmExports['malloc'](arr.length);
                HEAPU8.set(arr, ptr);
                finish(ptr, arr.length, status, hdrs, 1);
            }).catch(function() {
                finish(0, 0, status, hdrs, 0);
            });
        }
    }).catch(function(err) {
        if (err && err.name === 'AbortError') { finish(0, 0, 0, 0, 0); return; }
        finish(0, 0, 0, 0, 0);
    });
})

EM_JS(void, nt_http_web_cancel, (int slot_index), {
    if (Module['_nt_http_controllers'] && Module['_nt_http_controllers'][slot_index]) {
        Module['_nt_http_controllers'][slot_index].abort();
        delete Module['_nt_http_controllers'][slot_index];
    }
})
/* clang-format on */

/* ---- EMSCRIPTEN_KEEPALIVE callbacks (called from JS) ---- */

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_progress(int slot_index, int generation, int received, int total) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    if (!slot || slot->generation != (uint16_t)generation) {
        return;
    }
    slot->received = (uint32_t)received;
    slot->total = (uint32_t)total;
    slot->state = (uint8_t)NT_HTTP_STATE_DOWNLOADING;
}

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_complete(int slot_index, int generation, uint8_t *data, int size, int status, char *resp_headers, int success) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    if (!slot || slot->generation != (uint16_t)generation) {
        free(data);
        free(resp_headers);
        return;
    }
    slot->data = data;
    slot->size = (uint32_t)size;
    slot->status = (uint16_t)status;
    slot->resp_headers = resp_headers;
    slot->state = success ? (uint8_t)NT_HTTP_STATE_DONE : (uint8_t)NT_HTTP_STATE_FAILED;
}

/* ---- Backend entry points ---- */

void nt_http_backend_init(void) {}

void nt_http_backend_shutdown(void) {}

void nt_http_backend_update(void) {}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    nt_http_web_fetch((int)slot_index, (int)slot->generation, slot->url, slot->method, slot->body, (int)slot->body_size, slot->headers, (int)slot->headers_size, (int)slot->timeout_ms);
}

void nt_http_backend_cancel(uint16_t slot_index) { nt_http_web_cancel((int)slot_index); }
