#include "http/nt_http_internal.h"

#include "core/nt_assert.h"

#include <emscripten.h>
#include <stdlib.h>

/* Promise handlers only fill a JS record per slot; nt_http_backend_update copies it into C inside
 * the game's frame. No wasm code runs under a promise handler, so the fetch catch-all cannot hide a
 * C fault, and a fault while delivering ends the frame like any other crash. */

/* ---- EM_JS fetch with AbortController ---- */

/* clang-format off */
/* EM_JS_DEPS keeps these through Closure; HEAPU8/HEAPU32 are always-present globals (no dep). */
EM_JS_DEPS(nt_http_web, "$UTF8ToString")

/* record.state holds nt_http_state_t values: 1 PENDING, 2 DOWNLOADING, 3 DONE, 4 FAILED */
EM_JS(void, nt_http_web_fetch, (int slot_index, const char *url_ptr, const char *method_ptr, const uint8_t *body_ptr,
                                int body_size, const char *headers_ptr, int headers_size, int timeout_ms), {
    if (!Module['_nt_http']) {
        Module['_nt_http'] = {};
    }
    var r = {controller: new AbortController(), timer: 0, state: 1, status: 0, headers: null, received: 0, total: 0, chunks: []};
    Module['_nt_http'][slot_index] = r;

    /* Quoted keys/calls survive Closure without relying on its fetch externs.
     * Invalid caller input (bad header name, forbidden value bytes) throws from
     * Headers/Request construction — fail the request instead of unwinding into C. */
    var init;
    try {
        init = { 'method': UTF8ToString(method_ptr), 'signal': r.controller.signal };
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
        r.state = 4;
        return;
    }

    if (timeout_ms > 0) {
        r.timer = setTimeout(function() { r.controller.abort(); }, timeout_ms);
    }

    function fail(err) {
        /* Network errors, body read errors and aborts (cancel/timeout). Log non-abort failures
         * (CORS/mixed-content are otherwise invisible) — parity with the native NT_LOG_ERROR. */
        if (!err || err.name !== 'AbortError') {
            console.error('ERROR [http] fetch failed slot=' + slot_index, err);
        }
        r.state = 4;
    }
    function add(chunk) {
        if (r.received + chunk.length > 0xFFFFFFFF) { /* C sizes are uint32: fail like native */
            console.error('ERROR [http] response too large slot=' + slot_index);
            r.controller.abort();
            r.state = 4;
            return false;
        }
        r.chunks.push(chunk);
        r.received += chunk.length;
        r.state = 2;
        return true;
    }
    fetch(UTF8ToString(url_ptr), init).then(function(response) {
        var block = "";
        response.headers.forEach(function(v, k) { block += k + ': ' + v + '\n'; });
        r.status = response.status;
        r.headers = block;
        r.total = parseInt(response.headers.get('Content-Length'), 10) || 0;

        /* Try ReadableStream for progress, fall back to arrayBuffer */
        if (response.body && typeof response.body.getReader === 'function') {
            var reader = response.body.getReader();
            /* Not returned: a promise chain over every chunk would stay alive until the end. */
            function pump() {
                reader.read().then(function(part) {
                    if (part.done) {
                        r.state = 3;
                        return;
                    }
                    if (add(part.value)) {
                        pump();
                    }
                }).catch(fail);
            }
            pump();
            return;
        }
        return response.arrayBuffer().then(function(buf) {
            if (add(new Uint8Array(buf))) {
                r.state = 3;
            }
        });
    }).catch(fail);
})

/* out: received, total, status, header block size with its NUL (0 = no headers yet) */
EM_JS(int, nt_http_web_poll, (int slot_index, uint32_t *out), {
    var r = Module['_nt_http'][slot_index];
    var o = out >> 2;
    HEAPU32[o] = r.received;
    HEAPU32[o + 1] = r.total;
    HEAPU32[o + 2] = r.status;
    HEAPU32[o + 3] = r.headers === null ? 0 : r.headers.length + 1;
    return r.state;
})

/* Copies the body into data and the header block into hdrs (NULL drops either), then forgets the record. */
EM_JS(void, nt_http_web_take, (int slot_index, uint8_t *data, char *hdrs), {
    var r = Module['_nt_http'][slot_index];
    if (data) {
        for (var i = 0, at = data; i < r.chunks.length; at += r.chunks[i].length, i++) {
            HEAPU8.set(r.chunks[i], at);
        }
    }
    if (hdrs) {
        /* Header values are ByteStrings (code units <= 0xFF): write the bytes
         * directly — byte-exact parity with the native backend, no UTF-8 re-encode */
        for (var j = 0; j < r.headers.length; j++) {
            HEAPU8[hdrs + j] = r.headers.charCodeAt(j) & 0xFF;
        }
        HEAPU8[hdrs + r.headers.length] = 0;
    }
    clearTimeout(r.timer);
    delete Module['_nt_http'][slot_index];
})

/* Late promise handlers of a cancelled fetch write only to the dropped record. */
EM_JS(void, nt_http_web_cancel, (int slot_index), {
    var r = Module['_nt_http'][slot_index];
    clearTimeout(r.timer);
    r.controller.abort();
    delete Module['_nt_http'][slot_index];
})
/* clang-format on */

/* ---- Backend entry points ---- */

static bool web_in_flight(const NtHttpSlot *slot) { return slot->state == (uint8_t)NT_HTTP_STATE_PENDING || slot->state == (uint8_t)NT_HTTP_STATE_DOWNLOADING; }

bool nt_http_backend_init(void) { return true; }

void nt_http_backend_shutdown(void) {
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        if (web_in_flight(nt_http_get_slot(i))) {
            nt_http_web_cancel((int)i);
        }
    }
}

void nt_http_backend_update(void) {
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        NtHttpSlot *slot = nt_http_get_slot(i);
        if (!web_in_flight(slot)) {
            continue;
        }
        uint32_t info[4]; /* received, total, status, header block size */
        int state = nt_http_web_poll((int)i, info);
        slot->received = info[0];
        slot->total = info[1];
        if (state == NT_HTTP_STATE_PENDING || state == NT_HTTP_STATE_DOWNLOADING) {
            slot->state = (uint8_t)state;
            continue;
        }

        slot->status = (uint16_t)info[2];
        uint8_t *data = state == NT_HTTP_STATE_DONE && info[0] > 0 ? malloc(info[0]) : NULL;
        char *hdrs = info[3] > 0 ? malloc(info[3]) : NULL;
        /* OOM fails the request; a FAILED request keeps any headers that arrived */
        bool done = state == NT_HTTP_STATE_DONE && (data != NULL || info[0] == 0) && (hdrs != NULL || info[3] == 0);
        if (!done) {
            free(data);
            data = NULL;
        }
        nt_http_web_take((int)i, data, hdrs);
        slot->data = data;
        slot->resp_headers = hdrs;
        if (done) {
            /* Mid-transfer progress counts stream (decoded) bytes vs a possibly compressed
             * Content-Length; settle on received==total==size for a completed request */
            slot->size = info[0];
            slot->total = info[0];
            slot->state = (uint8_t)NT_HTTP_STATE_DONE;
        } else {
            slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
        }
    }
}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    NT_ASSERT(slot != NULL);
    nt_http_web_fetch((int)slot_index, slot->url, slot->method, slot->body, (int)slot->body_size, slot->headers, (int)slot->headers_size, (int)slot->timeout_ms);
}

void nt_http_backend_cancel(uint16_t slot_index) { nt_http_web_cancel((int)slot_index); }
