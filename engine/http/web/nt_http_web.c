#include "http/nt_http_internal.h"

#include <emscripten.h>

/* ---- EM_JS fetch with AbortController ---- */

/* clang-format off */
EM_JS(void, nt_http_web_fetch, (int slot_index, int generation, const char *url_ptr), {
    var url = UTF8ToString(url_ptr);

    if (!Module['_nt_http_controllers']) {
        Module['_nt_http_controllers'] = {};
    }
    var controller = new AbortController();
    Module['_nt_http_controllers'][slot_index] = controller;

    console.log('INFO [http] fetch start slot=' + slot_index + ' gen=' + generation + ' url=' + url);

    fetch(url, { signal: controller.signal }).then(function(response) {
        console.log('INFO [http] response slot=' + slot_index + ' status=' + response.status + ' ok=' + response.ok + ' hasBody=' + !!response.body);
        if (!response.ok) {
            Module['_nt_http_web_on_complete'](slot_index, generation, 0, 0, 0);
            return;
        }

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
                        console.log('INFO [http] stream done slot=' + slot_index + ' size=' + totalLen);
                        var ptr = Module['_malloc'](totalLen);
                        var offset = 0;
                        for (var i = 0; i < chunks.length; i++) {
                            Module['HEAPU8'].set(chunks[i], ptr + offset);
                            offset += chunks[i].length;
                        }
                        Module['_nt_http_web_on_complete'](slot_index, generation, ptr, totalLen, 1);
                        return;
                    }
                    var chunk = result.value;
                    chunks.push(chunk);
                    received += chunk.length;
                    Module['_nt_http_web_on_progress'](slot_index, generation, received, total);
                    pump();
                }).catch(function(e) {
                    console.error('ERROR [http] stream error slot=' + slot_index, e);
                    Module['_nt_http_web_on_complete'](slot_index, generation, 0, 0, 0);
                });
            }
            pump();
        } else {
            /* Fallback: no streaming progress */
            response.arrayBuffer().then(function(buf) {
                var arr = new Uint8Array(buf);
                var ptr = Module['_malloc'](arr.length);
                Module['HEAPU8'].set(arr, ptr);
                Module['_nt_http_web_on_complete'](slot_index, generation, ptr, arr.length, 1);
            }).catch(function() {
                Module['_nt_http_web_on_complete'](slot_index, generation, 0, 0, 0);
            });
        }
    }).catch(function(err) {
        if (err && err.name === 'AbortError') return;
        Module['_nt_http_web_on_complete'](slot_index, generation, 0, 0, 0);
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

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_complete(int slot_index, int generation, uint8_t *data, int size, int success) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    /* Debug log */
    emscripten_log(0, "INFO [http] cb slot=%d cb_gen=%d slot_gen=%u ok=%d sz=%d", slot_index, generation, slot ? (unsigned)slot->generation : 0, success, size);
    if (!slot || slot->generation != (uint16_t)generation) {
        if (data) {
            free(data);
        }
        return;
    }
    slot->data = data;
    slot->size = (uint32_t)size;
    slot->state = success ? (uint8_t)NT_HTTP_STATE_DONE : (uint8_t)NT_HTTP_STATE_FAILED;
}

/* ---- Backend entry points ---- */

void nt_http_backend_request(uint16_t slot_index, const char *url) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    int gen = slot ? (int)slot->generation : 0;
    nt_http_web_fetch((int)slot_index, gen, url);
}

void nt_http_backend_cancel(uint16_t slot_index) { nt_http_web_cancel((int)slot_index); }
