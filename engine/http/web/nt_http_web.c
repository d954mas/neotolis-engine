#include "http/nt_http.h"

#include <emscripten.h>

/* ---- Slot data (defined in nt_http.c) ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t received;
    uint32_t total;
    uint16_t generation;
    uint8_t state; /* nt_http_state_t */
    uint8_t _pad;
} NtHttpSlot;

extern NtHttpSlot *nt_http_get_slot(uint16_t slot_index);

/* ---- EM_JS fetch with ReadableStream progress ---- */

/* clang-format off */
EM_JS(void, nt_http_web_fetch, (int slot_index, const char *url_ptr), {
    var url = UTF8ToString(url_ptr);

    fetch(url).then(function(response) {
        if (!response.ok) {
            Module['_nt_http_web_on_complete'](slot_index, 0, 0, 0);
            return;
        }

        var contentLength = response.headers.get('Content-Length');
        var total = contentLength ? parseInt(contentLength, 10) : 0;
        var received = 0;
        var chunks = [];

        var reader = response.body.getReader();

        function pump() {
            reader.read().then(function(result) {
                if (result.done) {
                    /* Combine chunks into single buffer */
                    var totalLen = received;
                    var ptr = Module._malloc(totalLen);
                    var offset = 0;
                    for (var i = 0; i < chunks.length; i++) {
                        Module.HEAPU8.set(chunks[i], ptr + offset);
                        offset += chunks[i].length;
                    }
                    Module['_nt_http_web_on_complete'](slot_index, ptr, totalLen, 1);
                    return;
                }

                var chunk = result.value;
                chunks.push(chunk);
                received += chunk.length;
                Module['_nt_http_web_on_progress'](slot_index, received, total);
                pump();
            }).catch(function() {
                Module['_nt_http_web_on_complete'](slot_index, 0, 0, 0);
            });
        }

        pump();
    }).catch(function() {
        Module['_nt_http_web_on_complete'](slot_index, 0, 0, 0);
    });
})
/* clang-format on */

/* ---- EMSCRIPTEN_KEEPALIVE callbacks (called from JS) ---- */

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_progress(int slot_index, int received, int total) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    if (!slot) {
        return;
    }
    slot->received = (uint32_t)received;
    slot->total = (uint32_t)total;
    slot->state = (uint8_t)NT_HTTP_STATE_DOWNLOADING;
}

EMSCRIPTEN_KEEPALIVE void nt_http_web_on_complete(int slot_index, uint8_t *data, int size, int success) {
    NtHttpSlot *slot = nt_http_get_slot((uint16_t)slot_index);
    if (!slot) {
        return;
    }
    slot->data = data;
    slot->size = (uint32_t)size;
    slot->state = success ? (uint8_t)NT_HTTP_STATE_DONE : (uint8_t)NT_HTTP_STATE_FAILED;
}

/* ---- Backend entry point ---- */

void nt_http_backend_request(uint16_t slot_index, const char *url) { nt_http_web_fetch((int)slot_index, url); }
