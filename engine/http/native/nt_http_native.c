#include "http/nt_http_internal.h"

#include "log/nt_log.h"

/* curl's setopt/getinfo typecheck macros explode clang-tidy (sizeof tricks, huge
 * expansions); plain function calls keep the same runtime behavior. */
#define CURL_DISABLE_TYPECHECK

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native backend: libcurl multi interface, single-threaded. Transfers advance
 * only inside nt_http_backend_update (curl_multi_perform) — no locks needed. */

typedef struct {
    CURL *easy;
    struct curl_slist *req_headers;
    uint8_t *buf;
    uint32_t buf_size;
    uint32_t buf_cap;
    char *hdr;
    uint32_t hdr_size;
    uint32_t hdr_cap;
} NtHttpNativeXfer;

static struct {
    CURLM *multi;
    NtHttpNativeXfer xfers[NT_HTTP_MAX_REQUESTS + 1]; /* index 0 reserved, mirrors slot indices */
} s_native;

/* ---- Growing buffers (response body / headers land here until completion) ---- */

static void native_buf_append(uint8_t **buf, uint32_t *size, uint32_t *cap, const void *src, size_t n) {
    if (*size + n > *cap) {
        uint32_t new_cap = *cap ? *cap : 1024;
        while (*size + n > new_cap) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(*buf, new_cap);
        if (grown == NULL) {
            return; /* keep old buffer; transfer will be truncated and curl aborts on OOM elsewhere */
        }
        *buf = grown;
        *cap = new_cap;
    }
    memcpy(*buf + *size, src, n);
    *size += (uint32_t)n;
}

static void native_xfer_free_buffers(NtHttpNativeXfer *xfer) {
    free(xfer->buf);
    free(xfer->hdr);
    memset(xfer, 0, sizeof(*xfer));
}

/* ---- curl callbacks (invoked from curl_multi_perform on the caller's thread) ---- */

static size_t native_on_body(char *ptr, size_t size, size_t nmemb, void *userdata) {
    NtHttpNativeXfer *xfer = userdata;
    size_t n = size * nmemb;
    native_buf_append(&xfer->buf, &xfer->buf_size, &xfer->buf_cap, ptr, n);
    return n;
}

static size_t native_on_header(char *line, size_t size, size_t nmemb, void *userdata) {
    NtHttpNativeXfer *xfer = userdata;
    size_t n = size * nmemb;

    /* New status line (redirect / 100-continue): previous response's headers are obsolete */
    if (n >= 5 && strncmp(line, "HTTP/", 5) == 0) {
        xfer->hdr_size = 0;
        return n;
    }

    /* Find "name: value"; skip the blank terminator line and malformed lines */
    const char *colon = memchr(line, ':', n);
    if (colon == NULL) {
        return n;
    }
    size_t name_len = (size_t)(colon - line);
    const char *value = colon + 1;
    size_t value_len = n - name_len - 1;
    while (value_len > 0 && (*value == ' ' || *value == '\t')) {
        value++;
        value_len--;
    }
    while (value_len > 0 && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n')) {
        value_len--;
    }

    /* Append "name: value\n" with the name lowercased (parity with the web backend) */
    for (size_t i = 0; i < name_len; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        native_buf_append((uint8_t **)&xfer->hdr, &xfer->hdr_size, &xfer->hdr_cap, &c, 1);
    }
    native_buf_append((uint8_t **)&xfer->hdr, &xfer->hdr_size, &xfer->hdr_cap, ": ", 2);
    native_buf_append((uint8_t **)&xfer->hdr, &xfer->hdr_size, &xfer->hdr_cap, value, value_len);
    native_buf_append((uint8_t **)&xfer->hdr, &xfer->hdr_size, &xfer->hdr_cap, "\n", 1);
    return n;
}

static uint16_t native_xfer_index(const NtHttpNativeXfer *xfer) { return (uint16_t)(xfer - s_native.xfers); }

static int native_on_progress(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    NtHttpSlot *slot = nt_http_get_slot(native_xfer_index(userdata));
    if (slot != NULL && dlnow > 0) {
        slot->received = (uint32_t)dlnow;
        slot->total = (uint32_t)dltotal;
        slot->state = (uint8_t)NT_HTTP_STATE_DOWNLOADING;
    }
    return 0;
}

/* ---- Completion (from curl_multi_info_read) ---- */

static void native_finish(NtHttpNativeXfer *xfer, CURLcode result) {
    NtHttpSlot *slot = nt_http_get_slot(native_xfer_index(xfer));

    long code = 0;
    curl_easy_getinfo(xfer->easy, CURLINFO_RESPONSE_CODE, &code);
    slot->status = (uint16_t)code;

    /* Hand over the header block NUL-terminated (may exist even on failure) */
    if (xfer->hdr != NULL) {
        char zero = '\0';
        native_buf_append((uint8_t **)&xfer->hdr, &xfer->hdr_size, &xfer->hdr_cap, &zero, 1);
        slot->resp_headers = xfer->hdr;
        xfer->hdr = NULL;
    }

    if (result == CURLE_OK) {
        slot->data = xfer->buf;
        slot->size = xfer->buf_size;
        xfer->buf = NULL;
        slot->state = (uint8_t)NT_HTTP_STATE_DONE;
    } else {
        NT_LOG_ERROR("request failed: %s (%s)", curl_easy_strerror(result), slot->url);
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
    }

    curl_multi_remove_handle(s_native.multi, xfer->easy);
    curl_easy_cleanup(xfer->easy);
    curl_slist_free_all(xfer->req_headers);
    native_xfer_free_buffers(xfer);
}

/* ---- Backend entry points ---- */

void nt_http_backend_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    s_native.multi = curl_multi_init();
}

void nt_http_backend_shutdown(void) {
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        nt_http_backend_cancel(i);
    }
    if (s_native.multi != NULL) {
        curl_multi_cleanup(s_native.multi);
        s_native.multi = NULL;
    }
    curl_global_cleanup();
}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    NtHttpNativeXfer *xfer = &s_native.xfers[slot_index];

    CURL *easy = curl_easy_init();
    if (easy == NULL || s_native.multi == NULL) {
        if (easy != NULL) {
            curl_easy_cleanup(easy);
        }
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
        return;
    }

    curl_easy_setopt(easy, CURLOPT_URL, slot->url);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, xfer);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L); /* parity with fetch() default */
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 16L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, native_on_body);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, xfer);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, native_on_header);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, xfer);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, native_on_progress);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, xfer);
    if (slot->timeout_ms > 0) {
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, (long)slot->timeout_ms);
    }

    if (slot->body != NULL) {
        /* slot->body outlives the transfer (freed only on nt_http_free), so no copy */
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, (const char *)slot->body);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)slot->body_size);
    }
    if (strcmp(slot->method, "GET") != 0 && !(slot->body != NULL && strcmp(slot->method, "POST") == 0)) {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, slot->method);
    }

    struct curl_slist *headers = NULL;
    const char *p = slot->headers;
    for (uint32_t i = 0; i < slot->header_pairs; i++) {
        const char *name = p;
        p += strlen(p) + 1;
        const char *value = p;
        p += strlen(p) + 1;
        size_t line_len = strlen(name) + 2 + strlen(value) + 1;
        char *line = malloc(line_len);
        if (line != NULL) {
            (void)snprintf(line, line_len, "%s: %s", name, value);
            headers = curl_slist_append(headers, line);
            free(line);
        }
    }
    if (slot->body != NULL) {
        /* curl defaults Content-Type to x-www-form-urlencoded and sends Expect: 100-continue
         * for large bodies — both wrong for an opaque game payload */
        headers = curl_slist_append(headers, "Expect:");
    }
    if (headers != NULL) {
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    }
    xfer->req_headers = headers;
    xfer->easy = easy;

    if (curl_multi_add_handle(s_native.multi, easy) != CURLM_OK) {
        curl_easy_cleanup(easy);
        curl_slist_free_all(headers);
        memset(xfer, 0, sizeof(*xfer));
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
    }
}

void nt_http_backend_cancel(uint16_t slot_index) {
    NtHttpNativeXfer *xfer = &s_native.xfers[slot_index];
    if (xfer->easy == NULL) {
        return;
    }
    curl_multi_remove_handle(s_native.multi, xfer->easy);
    curl_easy_cleanup(xfer->easy);
    curl_slist_free_all(xfer->req_headers);
    native_xfer_free_buffers(xfer);
}

void nt_http_backend_update(void) {
    if (s_native.multi == NULL) {
        return;
    }
    int running = 0;
    curl_multi_perform(s_native.multi, &running);

    CURLMsg *msg = NULL;
    int msgs_left = 0;
    while ((msg = curl_multi_info_read(s_native.multi, &msgs_left)) != NULL) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }
        void *priv = NULL;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv);
        native_finish(priv, msg->data.result);
    }
}
