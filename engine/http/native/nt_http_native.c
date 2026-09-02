#include "http/nt_http_internal.h"

#include "core/nt_assert.h"
#include "log/nt_log.h"

/* curl's setopt/getinfo typecheck macros explode clang-tidy (sizeof tricks, huge
 * expansions); plain function calls keep the same runtime behavior. */
#define CURL_DISABLE_TYPECHECK

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CURLFOLLOW_OBEYCODE (redirect contract) appeared in 8.13 — a consumer-provided
 * older curl would fail on it with an unhelpful undeclared-identifier error */
#if LIBCURL_VERSION_NUM < 0x080D00
#error "nt_http native backend requires libcurl >= 8.13"
#endif

/* Native backend: libcurl multi interface, single-threaded. Transfers advance
 * only inside nt_http_backend_update (curl_multi_perform) — no locks needed.
 * The core guarantees backend entry points run only between a successful
 * backend_init and backend_shutdown, and slot->method arrives fetch-normalized
 * (canonical uppercase for the six known methods) — plain strcmp suffices. */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t cap;
} NtHttpBuf;

typedef struct {
    CURL *easy;
    struct curl_slist *req_headers;
    NtHttpBuf body;
    NtHttpBuf hdr;
} NtHttpNativeXfer;

static struct {
    CURLM *multi;
    NtHttpNativeXfer xfers[NT_HTTP_MAX_REQUESTS + 1]; /* index 0 reserved, mirrors slot indices */
} s_native;

/* ---- Growing buffers (response body / headers land here until completion) ---- */

/* false on realloc failure or a response outgrowing uint32 sizes — the caller must
 * abort the transfer (return 0 to curl) so a short buffer is never published as DONE. */
static bool native_buf_append(NtHttpBuf *b, const void *src, size_t n) {
    if (n == 0) {
        return true; /* memcpy on a still-NULL buffer would be UB even with n=0 */
    }
    uint64_t need = (uint64_t)b->size + n;
    if (need > 0xFFFFFFFFULL) {
        return false;
    }
    if (need > b->cap) {
        uint64_t new_cap = b->cap ? b->cap : 1024;
        while (new_cap < need) {
            new_cap *= 2;
        }
        if (new_cap > 0xFFFFFFFFULL) {
            new_cap = 0xFFFFFFFFULL;
        }
        uint8_t *grown = realloc(b->data, (size_t)new_cap);
        if (grown == NULL) {
            return false;
        }
        b->data = grown;
        b->cap = (uint32_t)new_cap;
    }
    NT_ASSERT(b->data != NULL); /* n > 0 forces the grow branch while data is NULL */
    memcpy(b->data + b->size, src, n);
    b->size += (uint32_t)n;
    return true;
}

static void native_xfer_free_buffers(NtHttpNativeXfer *xfer) {
    free(xfer->body.data);
    free(xfer->hdr.data);
    memset(xfer, 0, sizeof(*xfer));
}

/* ---- curl callbacks (invoked from curl_multi_perform on the caller's thread) ---- */

static size_t native_on_body(char *ptr, size_t size, size_t nmemb, void *userdata) {
    NtHttpNativeXfer *xfer = userdata;
    size_t n = size * nmemb;
    /* 0 -> CURLE_WRITE_ERROR -> FAILED: never deliver a truncated body as DONE */
    return native_buf_append(&xfer->body, ptr, n) ? n : 0;
}

static size_t native_on_header(char *line, size_t size, size_t nmemb, void *userdata) {
    NtHttpNativeXfer *xfer = userdata;
    size_t n = size * nmemb;

    /* New status line (redirect / 100-continue): previous response's headers are obsolete */
    if (n >= 5 && strncmp(line, "HTTP/", 5) == 0) {
        xfer->hdr.size = 0;
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

    /* Append "name: value\n" with the name lowercased in place (parity with the web
     * backend). Any failed append aborts the transfer (0 -> CURLE_WRITE_ERROR), so a
     * partial line never survives into a published header block. */
    uint32_t name_off = xfer->hdr.size;
    bool ok = native_buf_append(&xfer->hdr, line, name_len);
    for (uint32_t i = name_off; ok && i < xfer->hdr.size; i++) {
        if (xfer->hdr.data[i] >= 'A' && xfer->hdr.data[i] <= 'Z') {
            xfer->hdr.data[i] = (uint8_t)(xfer->hdr.data[i] + ('a' - 'A'));
        }
    }
    ok = ok && native_buf_append(&xfer->hdr, ": ", 2);
    ok = ok && native_buf_append(&xfer->hdr, value, value_len);
    ok = ok && native_buf_append(&xfer->hdr, "\n", 1);
    return ok ? n : 0;
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
    NT_ASSERT(slot != NULL);

    long code = 0;
    curl_easy_getinfo(xfer->easy, CURLINFO_RESPONSE_CODE, &code);
    slot->status = (uint16_t)code;

    /* Hand over the header block NUL-terminated (may exist even on failure); a
     * headerless success still hands over "" — web never returns NULL after DONE */
    if (xfer->hdr.data != NULL || result == CURLE_OK) {
        char zero = '\0';
        if (native_buf_append(&xfer->hdr, &zero, 1)) {
            slot->resp_headers = (char *)xfer->hdr.data;
            xfer->hdr.data = NULL;
        } /* else: unterminated block stays in xfer and is freed below */
    }

    if (result == CURLE_OK) {
        slot->data = xfer->body.data;
        slot->size = xfer->body.size;
        xfer->body.data = NULL;
        /* Mid-transfer progress counts wire (possibly compressed) bytes; settle on
         * the decoded size so received==total==size holds for a completed request */
        slot->received = slot->size;
        slot->total = slot->size;
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

bool nt_http_backend_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        NT_LOG_ERROR("curl_global_init failed");
        return false;
    }
    s_native.multi = curl_multi_init();
    if (s_native.multi == NULL) {
        NT_LOG_ERROR("curl_multi_init failed");
        curl_global_cleanup();
        return false;
    }
    /* A consumer-provided libcurl without async DNS would block nt_http_update
     * (and the frame) on every hostname resolve, silently breaking the
     * non-blocking frame contract — refuse init instead (the vendored build
     * always has the threaded resolver). */
    curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (info != NULL && (info->features & CURL_VERSION_ASYNCHDNS) == 0) {
        NT_LOG_ERROR("libcurl built without async DNS — hostname resolves would block the frame");
        curl_multi_cleanup(s_native.multi);
        s_native.multi = NULL;
        curl_global_cleanup();
        return false;
    }
    /* No decoders -> curl simply never negotiates compression (correct, identity
     * bytes) — warn about the web-parity gap, do not fail */
    if (info != NULL && (info->features & CURL_VERSION_LIBZ) == 0) {
        NT_LOG_WARN("libcurl built without zlib — compression will not be negotiated (fetch always does)");
    }
    return true;
}

void nt_http_backend_shutdown(void) {
    NT_ASSERT(s_native.multi != NULL);
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        nt_http_backend_cancel(i);
    }
    curl_multi_cleanup(s_native.multi);
    s_native.multi = NULL;
    curl_global_cleanup();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_ASSERT expansions
static struct curl_slist *native_build_headers(const NtHttpSlot *slot) {
    struct curl_slist *headers = NULL;
    if (slot->body == NULL && strcmp(slot->method, "POST") == 0) {
        /* Empty POSTFIELDS makes curl invent x-www-form-urlencoded; fetch sends no
         * Content-Type here. First in the list, so a caller's explicit pair (appended
         * later) still re-adds one. */
        headers = curl_slist_append(headers, "Content-Type:");
        NT_ASSERT(headers != NULL);
    }
    const char *p = slot->headers;
    for (uint32_t i = 0; i < slot->header_pairs; i++) {
        const char *name = p;
        p += strlen(p) + 1;
        const char *value = p;
        p += strlen(p) + 1;
        size_t line_len = strlen(name) + 2 + strlen(value) + 1;
        char *line = malloc(line_len);
        NT_ASSERT(line != NULL);
        /* "Name;" is curl's syntax for an empty value — a plain "Name: " would be
         * silently dropped, diverging from the web backend which sends it */
        (void)snprintf(line, line_len, *value != '\0' ? "%s: %s" : "%s;%s", name, value);
        struct curl_slist *appended = curl_slist_append(headers, line);
        NT_ASSERT(appended != NULL);
        headers = appended;
        free(line);
    }
    if (slot->body != NULL) {
        /* curl defaults Content-Type to x-www-form-urlencoded and sends Expect: 100-continue
         * for large bodies — both wrong for an opaque game payload */
        struct curl_slist *appended = curl_slist_append(headers, "Expect:");
        NT_ASSERT(appended != NULL);
        headers = appended;
    }
    return headers;
}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    NT_ASSERT(slot != NULL);
    NT_ASSERT(s_native.multi != NULL);
    NtHttpNativeXfer *xfer = &s_native.xfers[slot_index];

    CURL *easy = curl_easy_init();
    if (easy == NULL) {
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
        return;
    }

    curl_easy_setopt(easy, CURLOPT_URL, slot->url);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, xfer);
    /* OBEYCODE = RFC redirect semantics: 303 -> GET always; 301/302 -> GET for any
     * request in POST mode, i.e. anything sent via POSTFIELDS (so PUT+body demotes
     * too — browsers keep it; divergence documented in the spec); bodiless custom
     * methods keep their verb on 301/302 */
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, CURLFOLLOW_OBEYCODE);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 16L);
    /* "" = advertise and transparently decode every built-in encoding (gzip/deflate
     * via vendored zlib) — parity with fetch(), which always negotiates compression */
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
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
        /* slot->body outlives the transfer (freed only on nt_http_free), so no copy.
         * _LARGE avoids the 32-bit long truncation on LLP64 for 2 GiB+ bodies. */
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, (const char *)slot->body);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)slot->body_size);
    } else if (strcmp(slot->method, "POST") == 0) {
        /* A bodiless POST still needs POST mode + Content-Length: 0 (a bare
         * CUSTOMREQUEST would send neither and many servers answer 411) */
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)0);
    }
    if (strcmp(slot->method, "HEAD") == 0) {
        /* CUSTOMREQUEST "HEAD" would leave curl waiting for a body that never comes */
        curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    } else if (strcmp(slot->method, "GET") != 0 && strcmp(slot->method, "POST") != 0) {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, slot->method);
    }

    struct curl_slist *headers = native_build_headers(slot);
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
    NT_ASSERT(s_native.multi != NULL);
    int running = 0;
    CURLMcode rc = curl_multi_perform(s_native.multi, &running);
    if (rc != CURLM_OK) {
        /* Multi machinery broke (OOM/internal error): without this the in-flight
         * requests would sit PENDING forever with no signal */
        NT_LOG_ERROR("curl_multi_perform: %s", curl_multi_strerror(rc));
        for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
            if (s_native.xfers[i].easy != NULL) {
                NtHttpSlot *slot = nt_http_get_slot(i);
                if (slot != NULL) {
                    slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
                }
                nt_http_backend_cancel(i);
            }
        }
        return;
    }

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
