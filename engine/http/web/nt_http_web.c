#include "http/nt_http_internal.h"

#include "core/nt_assert.h"

#include <emscripten.h>
#include <stdlib.h>

/* ---- EM_JS fetch with AbortController ---- */

/* clang-format off */
/* EM_JS_DEPS keeps these through Closure. malloc (linker-added, no leading _) is kept as a wasm
 * export for wasmExports['malloc']; HEAPU8 is an always-present global (no dep). */
EM_JS_DEPS(nt_http_web, "$UTF8ToString,malloc")

EM_JS(void, nt_http_web_fetch, (int slot_index, int generation, int epoch, const char *url_ptr, const char *method_ptr,
                                const uint8_t *body_ptr, int body_size, const char *headers_ptr, int headers_size,
                                int timeout_ms), {
    var url = UTF8ToString(url_ptr);

    if (!Module['_nt_http_controllers']) {
        Module['_nt_http_controllers'] = {};
    }
    var controller = new AbortController();
    Module['_nt_http_controllers'][slot_index] = controller;

    var timer = 0;
    var status = 0;
    var hdrs = 0; /* wasm-heap block; owned here until handed to on_complete exactly once */
    var finished = false;

    function finish(ptr, size, success) {
        if (finished) return; /* a throw inside the first finish must not complete twice */
        finished = true;
        if (timer) clearTimeout(timer);
        /* The slot may already be reused by a newer request — only drop OUR controller */
        if (Module['_nt_http_controllers'][slot_index] === controller) {
            delete Module['_nt_http_controllers'][slot_index];
        }
        _nt_http_web_on_complete(slot_index, generation, epoch, ptr, size, status, hdrs, success);
        hdrs = 0;
    }
    function headerBlock(response) {
        var s = "";
        response.headers.forEach(function(v, k) { s += k + ': ' + v + '\n'; });
        /* Headers values are ByteStrings (code units <= 0xFF): write the bytes
         * directly — byte-exact parity with the native backend, no UTF-8 re-encode */
        var p = wasmExports['malloc'](s.length + 1);
        if (!p) return 0;
        for (var i = 0; i < s.length; i++) {
            HEAPU8[p + i] = s.charCodeAt(i) & 0xFF;
        }
        HEAPU8[p + s.length] = 0;
        return p;
    }

    /* Quoted keys/calls survive Closure without relying on its fetch externs.
     * Invalid caller input (bad header name, forbidden value bytes) throws from
     * Headers/Request construction — fail the request instead of unwinding into C. */
    var init;
    try {
        init = { 'method': UTF8ToString(method_ptr), 'signal': controller.signal };
        if (headers_size > 0) {
            /* Alternating NUL-terminated name,value byte strings. Bytes map 1:1 to
             * code units (true Latin-1) so non-ASCII values stay ByteString-legal and
             * reach the wire byte-exact; TextDecoder('latin1') would remap 0x80-0x9F. */
            var raw = HEAPU8.subarray(headers_ptr, headers_ptr + headers_size);
            var s = "";
            for (var i = 0; i < raw.length; i++) {
                s += String.fromCharCode(raw[i]);
            }
            var parts = s.split('\0');
            var headers = new Headers();
            for (var i = 0; i + 1 < parts.length; i += 2) {
                headers['append'](parts[i], parts[i + 1]);
            }
            init['headers'] = headers;
        }
        if (body_size > 0) {
            init['body'] = HEAPU8.slice(body_ptr, body_ptr + body_size);
        }
    } catch (e) {
        console.error('ERROR [http] bad request options slot=' + slot_index, e);
        finish(0, 0, 0);
        return;
    }

    if (timeout_ms > 0) {
        timer = setTimeout(function() { controller.abort(); }, timeout_ms);
    }

    fetch(url, init).then(function(response) {
        status = response.status;
        hdrs = headerBlock(response);
        if (!hdrs) { finish(0, 0, 0); return; } /* OOM: FAILED — native aborts on the same OOM */

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
                        if (totalLen > 0 && !ptr) { finish(0, 0, 0); return; } /* OOM: FAILED, never write at 0 */
                        var offset = 0;
                        for (var i = 0; i < chunks.length; i++) {
                            HEAPU8.set(chunks[i], ptr + offset);
                            offset += chunks[i].length;
                        }
                        finish(ptr, totalLen, 1);
                        return;
                    }
                    var chunk = result.value;
                    chunks.push(chunk);
                    received += chunk.length;
                    /* Clamp: the int params would wrap past 2^31 (huge bodies fail later at malloc anyway) */
                    _nt_http_web_on_progress(slot_index, generation, epoch,
                                             received > 0x7FFFFFFF ? 0x7FFFFFFF : received,
                                             total > 0x7FFFFFFF ? 0x7FFFFFFF : total);
                    pump();
                }).catch(function(e) {
                    console.error('ERROR [http] stream error slot=' + slot_index, e);
                    finish(0, 0, 0);
                });
            }
            pump();
        } else {
            /* Fallback: no streaming progress */
            response.arrayBuffer().then(function(buf) {
                var arr = new Uint8Array(buf);
                var ptr = wasmExports['malloc'](arr.length);
                if (arr.length > 0 && !ptr) { finish(0, 0, 0); return; }
                HEAPU8.set(arr, ptr);
                finish(ptr, arr.length, 1);
            }).catch(function() {
                finish(0, 0, 0);
            });
        }
    }).catch(function(err) {
        /* Covers network errors, aborts (cancel/timeout) and throws after headerBlock —
         * finish still owns hdrs, so the block is handed over, never leaked. Log
         * non-abort failures (CORS/mixed-content are otherwise invisible) — parity
         * with the native backend's NT_LOG_ERROR. */
        if (!err || err.name !== 'AbortError') {
            console.error('ERROR [http] fetch failed slot=' + slot_index, err);
        }
        finish(0, 0, 0);
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

/* Lifecycle epoch: bumped on every module init. Slot generations restart after a
 * shutdown/init cycle, so (slot, generation) alone cannot reject a callback from a
 * fetch that was started (and aborted) in a previous epoch of the module. */
static int s_web_epoch;

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_progress(int slot_index, int generation, int epoch, int received, int total) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    if (epoch != s_web_epoch || !slot || slot->generation != (uint16_t)generation) {
        return;
    }
    slot->received = (uint32_t)received;
    slot->total = (uint32_t)total;
    slot->state = (uint8_t)NT_HTTP_STATE_DOWNLOADING;
}

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_complete(int slot_index, int generation, int epoch, uint8_t *data, int size, int status, char *resp_headers, int success) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    if (epoch != s_web_epoch || !slot || slot->generation != (uint16_t)generation) {
        free(data);
        free(resp_headers);
        return;
    }
    slot->data = data;
    slot->size = (uint32_t)size;
    slot->status = (uint16_t)status;
    slot->resp_headers = resp_headers;
    if (success) {
        /* Mid-transfer progress counts stream (decoded) bytes vs a possibly compressed
         * Content-Length; settle on received==total==size for a completed request */
        slot->received = slot->size;
        slot->total = slot->size;
    }
    slot->state = success ? (uint8_t)NT_HTTP_STATE_DONE : (uint8_t)NT_HTTP_STATE_FAILED;
}

/* ---- Backend entry points ---- */

bool nt_http_backend_init(void) {
    s_web_epoch++; /* callbacks from any previous epoch's fetches are now rejected */
    return true;
}

/* Abort everything in flight: their (already-scheduled) completions are rejected by
 * the epoch check after the next init, freeing whatever they carry. */
void nt_http_backend_shutdown(void) {
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        nt_http_web_cancel((int)i);
    }
}

void nt_http_backend_update(void) {}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    NT_ASSERT(slot != NULL);
    nt_http_web_fetch((int)slot_index, (int)slot->generation, s_web_epoch, slot->url, slot->method, slot->body, (int)slot->body_size, slot->headers, (int)slot->headers_size, (int)slot->timeout_ms);
}

void nt_http_backend_cancel(uint16_t slot_index) { nt_http_web_cancel((int)slot_index); }
