// #region platform shim — this file IS the platform boundary
#ifdef _WIN32
/* clang-format off */
#include <winsock2.h> /* must precede windows.h — else winsock1 is pulled in and conflicts. */
#include <windows.h>   /* GetTickCount64 */
#include <ws2tcpip.h>
/* clang-format on */
typedef SOCKET nt_sock_t;
typedef int nt_socklen_t;
#define NT_INVALID_SOCK INVALID_SOCKET
#define nt_close_sock(s) closesocket(s)
#define nt_sock_errno() WSAGetLastError()
#define NT_EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <signal.h>
#include <sys/select.h> /* select() for writability waits */
#include <sys/socket.h>
#include <unistd.h>
typedef int nt_sock_t;
typedef socklen_t nt_socklen_t;
#define NT_INVALID_SOCK (-1)
#define nt_close_sock(s) close(s)
#define nt_sock_errno() errno
#define NT_EWOULDBLOCK EWOULDBLOCK
#endif
// #endregion

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_internal.h" /* nt_devapi_deferred_reset on client drop. */
#include "devapi/nt_devapi_net.h"

/* Single loopback client; reconnect allowed. */
static nt_sock_t s_listen = NT_INVALID_SOCK;
static nt_sock_t s_client = NT_INVALID_SOCK;

#ifdef _WIN32
/* WSAStartup refcount: balance cleanup so a failed start path doesn't over-call WSACleanup. */
static bool s_wsa_init = false;
#endif

/* Inbound request-line cap: an unterminated line past this is a framing desync / abuse on this
   dev-only channel -> drop the client. Bounds client->server REQUESTS only, not the (pixel-capped)
   capture response — that travels the other way, under the Python client's own recv cap. Overridable. */
#ifndef NT_DEVAPI_NET_MAX_LINE
#define NT_DEVAPI_NET_MAX_LINE ((size_t)1024U * 1024U)
#endif

/* Per-stall writability timeout (ms): a peer that cannot accept a byte within this window on the
   dev-only loopback channel is wedged -> drop it. Timed per EWOULDBLOCK stall and reset on every
   successful send, so a healthy but slow reader is never dropped — only a non-draining one. Overridable. */
#ifndef NT_DEVAPI_NET_SEND_TIMEOUT_MS
#define NT_DEVAPI_NET_SEND_TIMEOUT_MS 2000
#endif

/* Growing recv accumulation buffer; JSON-lines split on '\n'. s_recv_len bytes are the unconsumed
   remainder carried between polls/frames, bounded above by NT_DEVAPI_NET_MAX_LINE. */
static char *s_recv_buf;
static size_t s_recv_cap;
static size_t s_recv_len;

// #region recv buffer (geometric grow, mirrors resp_reserve in nt_devapi.c)
static void recv_reserve(size_t need) {
    if (need <= s_recv_cap) {
        return;
    }
    size_t cap = s_recv_cap ? s_recv_cap : 256U;
    while (cap < need) {
        if (cap > SIZE_MAX / 2U) {
            cap = need; /* geometric growth would wrap size_t — allocate exactly. */
            break;
        }
        cap *= 2U;
    }
    char *grown = (char *)realloc(s_recv_buf, cap);
    NT_ASSERT(grown != NULL);
    s_recv_buf = grown;
    s_recv_cap = cap;
}

static void recv_append(const char *p, size_t len) {
    NT_ASSERT(len <= SIZE_MAX - s_recv_len); /* guard the size_t addition from wrapping. */
    recv_reserve(s_recv_len + len);
    memcpy(s_recv_buf + s_recv_len, p, len);
    s_recv_len += len;
}
// #endregion

// #region client lifecycle
static void close_client(void) {
    if (s_client != NT_INVALID_SOCK) {
        nt_close_sock(s_client);
        s_client = NT_INVALID_SOCK;
    }
    s_recv_len = 0; /* drop any partial line — the next client starts clean. */
    /* Drop the gone client's in-flight deferred results so a reconnecting client
       can't receive them tagged with the old client's request_id. */
    nt_devapi_deferred_reset();
    /* Generic client-reset hooks: a compiled-out group registers none, so net.c names no group. */
    nt_devapi_run_reset_hooks();
    /* Resets ONLY devapi-owned transient state (deferred queue + reset-hook schedules). Game-owned
       state (time mode, gate, applied input) is the changer's job — L1 can't tell game from bot. */
}

static void set_nonblocking(nt_sock_t s) {
#ifdef _WIN32
    u_long nb = 1;
    int rc = ioctlsocket(s, (long)FIONBIO, &nb);
    NT_ASSERT(rc == 0); /* fresh socket — failure is a bug, not a runtime condition. */
    (void)rc;
#else
    int fl = fcntl(s, F_GETFL, 0);
    NT_ASSERT(fl != -1);
    int rc = fcntl(s, F_SETFL, fl | O_NONBLOCK);
    NT_ASSERT(rc != -1);
    (void)rc;
#endif
}

static void set_client_opts(nt_sock_t s) {
    int yes = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, (nt_socklen_t)sizeof yes);
#if defined(SO_NOSIGPIPE)
    (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&yes, (nt_socklen_t)sizeof yes); /* macOS/BSD */
#endif
    set_nonblocking(s);
}
// #endregion

// #region send (partial-send safe, yields on backpressure)
/* select() instead of a busy-spin on EWOULDBLOCK: a tight spin pins a core AND starves the loopback
   reader of the CPU it needs to drain — self-defeating, so a healthy reader gets falsely dropped.
   false = the timeout elapsed with the peer still not draining. */
static bool sock_wait_writable(nt_sock_t s, int timeout_ms) {
    for (;;) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(s, &wfds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (long)(timeout_ms % 1000) * 1000;
#ifdef _WIN32
        int rc = select(0, NULL, &wfds, NULL, &tv); /* nfds is ignored by Winsock. */
#else
        int rc = select((int)s + 1, NULL, &wfds, NULL, &tv);
        if (rc < 0 && nt_sock_errno() == EINTR) {
            continue; /* a signal is not a peer error — wait again. */
        }
#endif
        return rc > 0;
    }
}

/* On EWOULDBLOCK, wait for writability (yielding to the reader) instead of busy-spinning; a peer that
   never drains within timeout_ms is wedged -> false, and the caller drops it. */
static bool send_all(nt_sock_t s, const char *p, size_t len, int timeout_ms) {
    size_t off = 0;
    while (off < len) {
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL; /* suppress SIGPIPE on write-to-closed-peer (Linux). */
#endif
#ifdef _WIN32
        NT_ASSERT((len - off) <= (size_t)INT_MAX); /* send() takes int; guard the narrowing cast. */
        int n = send(s, p + off, (int)(len - off), flags);
#else
        ssize_t n = send(s, p + off, len - off, flags);
#endif
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        int e = nt_sock_errno();
        if (e == NT_EWOULDBLOCK) {
            if (!sock_wait_writable(s, timeout_ms)) {
                return false; /* buffer stayed full for the whole window — peer wedged, give up. */
            }
            continue;
        }
#ifndef _WIN32
        if (e == EINTR) {
            continue;
        }
#endif
        return false; /* peer gone / real error. */
    }
    return true;
}

/* Frame a response line: defence-in-depth — compact cJSON already escapes '\n' inside
   strings, so a raw '\n' before the framing newline would desync the client's line parser. */
static bool send_line(const char *resp) {
    size_t len = strlen(resp);
    NT_ASSERT(memchr(resp, '\n', len) == NULL);
    if (!send_all(s_client, resp, len, NT_DEVAPI_NET_SEND_TIMEOUT_MS)) {
        return false;
    }
    return send_all(s_client, "\n", 1U, NT_DEVAPI_NET_SEND_TIMEOUT_MS);
}
// #endregion

// #region start / stop
bool nt_devapi_net_start(uint16_t port) {
    NT_ASSERT(s_listen == NT_INVALID_SOCK); /* start is single-shot until stop. */
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
    s_wsa_init = true;
#else
    (void)signal(SIGPIPE, SIG_IGN); /* global fallback; MSG_NOSIGNAL/SO_NOSIGPIPE are the per-call guard. */
#endif

    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen == NT_INVALID_SOCK) {
#ifdef _WIN32
        WSACleanup();
        s_wsa_init = false;
#endif
        return false;
    }

    int yes = 1;
#ifdef _WIN32
    /* SO_REUSEADDR is a port-hijack vector on Windows — use EXCLUSIVE. */
    (void)setsockopt(s_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&yes, (nt_socklen_t)sizeof yes);
#else
    /* SO_REUSEADDR on POSIX avoids TIME_WAIT "address in use" on rapid restart. */
    (void)setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &yes, (nt_socklen_t)sizeof yes);
#endif
    /* No TCP_NODELAY here: it is a no-op on a listen socket. set_client_opts sets it per accept. */
    set_nonblocking(s_listen);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 ONLY — never INADDR_ANY. */

    if (bind(s_listen, (struct sockaddr *)&addr, (nt_socklen_t)sizeof addr) != 0 || listen(s_listen, 1) != 0) {
        nt_close_sock(s_listen);
        s_listen = NT_INVALID_SOCK;
#ifdef _WIN32
        WSACleanup();
        s_wsa_init = false;
#endif
        return false; /* port taken / refused — API-contract return, not an assert. */
    }
    /* Register recv+deferred-drain as a tick hook. Must precede the input group's tick hook so a
       line submitted this frame schedules its input edge BEFORE the input schedule tick runs — hosts
       therefore call net_start before register_default (hooks run in registration order). Register at
       most once: the table is append-only, so a stop->start restart must not enqueue a second poll. */
    if (!nt_devapi_tick_is_registered(nt_devapi_net_poll)) {
        nt_devapi_register_tick(nt_devapi_net_poll);
    }
    return true;
}

bool nt_devapi_net_has_client(void) { return s_client != NT_INVALID_SOCK; }

void nt_devapi_net_stop(void) {
    close_client();
    if (s_listen != NT_INVALID_SOCK) {
        nt_close_sock(s_listen);
        s_listen = NT_INVALID_SOCK;
    }
    free(s_recv_buf);
    s_recv_buf = NULL;
    s_recv_cap = 0;
    s_recv_len = 0;
#ifdef _WIN32
    if (s_wsa_init) {
        WSACleanup();
        s_wsa_init = false;
    }
#endif
}
// #endregion

// #region accept / poll
/* Non-blocking accept; sets the new client non-blocking + TCP_NODELAY. */
static void try_accept(void) {
    NT_ASSERT(s_client == NT_INVALID_SOCK); /* callers must not overwrite a live client. */
    nt_sock_t c = accept(s_listen, NULL, NULL);
    if (c != NT_INVALID_SOCK) {
        s_client = c;
        s_recv_len = 0;
        set_client_opts(c);
    }
    /* else EWOULDBLOCK == no pending connection — try again next frame. */
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_devapi_net_poll(void) {
    if (s_listen == NT_INVALID_SOCK) {
        return; /* not started / stopped. */
    }
    if (s_client == NT_INVALID_SOCK) {
        try_accept();
    }
    if (s_client == NT_INVALID_SOCK) {
        return;
    }

    // #region recv (orderly close vs no-data)
    char tmp[4096];
    for (;;) {
#ifdef _WIN32
        int n = recv(s_client, tmp, (int)sizeof tmp, 0);
#else
        ssize_t n = recv(s_client, tmp, sizeof tmp, 0);
#endif
        if (n > 0) {
            recv_append(tmp, (size_t)n);
            /* Bound peak growth within one poll: stop reading once past the cap so a hostile flood
               can't balloon the buffer before the post-dispatch cap check. Complete lines are still
               dispatched below; a too-long unterminated remainder then drops the client. */
            if (s_recv_len > NT_DEVAPI_NET_MAX_LINE) {
                break;
            }
            continue;
        }
        if (n == 0) {
            close_client(); /* orderly disconnect — distinct from EWOULDBLOCK. */
            return;
        }
        int e = nt_sock_errno();
        if (e == NT_EWOULDBLOCK) {
            break; /* no more data this frame. */
        }
#ifndef _WIN32
        if (e == EAGAIN || e == EINTR) {
            break;
        }
#endif
        close_client(); /* real error → drop client. */
        return;
    }
    // #endregion

    // #region framed dispatch — submit each line, write its response inline
    /* Read cursor across the accumulated buffer; the remainder is shifted to the front after
       the line loop, so the returned submit/poll_response pointers are never held across a
       recv_append. Skip entirely when empty: s_recv_buf may still be NULL, so memchr on it is UB. */
    if (s_recv_len > 0) {
        size_t cur = 0;
        for (;;) {
            char *nl = (char *)memchr(s_recv_buf + cur, '\n', s_recv_len - cur);
            if (nl == NULL) {
                break;
            }
            *nl = '\0';
            char *line = s_recv_buf + cur;
            cur = (size_t)(nl - s_recv_buf) + 1U;

            const char *resp = nt_devapi_submit(line);
            if (resp != NULL) {
                /* Write BEFORE the next core call — s_resp_buf is reused each submit. */
                if (!send_line(resp)) {
                    close_client();
                    return;
                }
            }
            /* resp == NULL → deferred: nothing now; arrives via the drain below. */
        }
        /* Shift the unconsumed remainder to the front. */
        if (cur > 0) {
            size_t rest = s_recv_len - cur;
            if (rest > 0) {
                memmove(s_recv_buf, s_recv_buf + cur, rest);
            }
            s_recv_len = rest;
        }
    }
    /* Inbound request-line cap: an unterminated remainder larger than the cap is a framing desync /
       abuse — drop the client (it may reconnect) rather than grow the buffer without limit. Complete
       lines were already consumed above. */
    if (s_recv_len > NT_DEVAPI_NET_MAX_LINE) {
        close_client();
        return;
    }
    // #endregion

    // #region deferred drain — emit every result whose game-frame deadline (g_nt_app.frame) has passed
    const char *dr;
    while ((dr = nt_devapi_poll_response()) != NULL) {
        if (!send_line(dr)) {
            close_client();
            return;
        }
    }
    // #endregion
}
// #endregion

// #region wait_for_client (opt-in pre-loop gate, bounded)
/* Monotonic WALL clock: clock() measures CPU time (wrong for a wall-clock spin timeout). */
static uint64_t now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    NT_ASSERT(rc == 0);
    (void)rc;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

bool nt_devapi_net_wait_for_client(uint32_t timeout_ms) {
    if (s_listen == NT_INVALID_SOCK) {
        return false;
    }
    if (s_client != NT_INVALID_SOCK) {
        return true;
    }
    uint64_t deadline = now_ms() + timeout_ms;
    for (;;) {
        try_accept();
        if (s_client != NT_INVALID_SOCK) {
            return true;
        }
        if (now_ms() >= deadline) {
            return false; /* bounded — never an unbounded spin. */
        }
    }
}
// #endregion

#ifdef NT_TEST_ACCESS
// #region test_access — self-contained loopback backpressure check
static bool sock_set_int(nt_sock_t s, int opt, int v) { return setsockopt(s, SOL_SOCKET, opt, (const char *)&v, (nt_socklen_t)sizeof v) == 0; }

static int sock_get_int(nt_sock_t s, int opt) {
    int v = 0;
    nt_socklen_t len = (nt_socklen_t)sizeof v;
    if (getsockopt(s, SOL_SOCKET, opt, (char *)&v, &len) != 0) {
        return 0;
    }
    return v;
}

/* Connected loopback pair: *w writer (non-blocking), *r reader (blocking). reader_rcvbuf is set BEFORE
   connect on purpose — only then does it cap the window AND disable Windows receive auto-tuning, so a
   small value actually induces backpressure on a non-reading peer. */
static bool loopback_pair(nt_sock_t *w, nt_sock_t *r, int writer_sndbuf, int reader_rcvbuf) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* ephemeral port. */
    nt_socklen_t alen = (nt_socklen_t)sizeof addr;
    nt_sock_t cli = NT_INVALID_SOCK;
    nt_sock_t srv = NT_INVALID_SOCK;
    nt_sock_t lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lst == NT_INVALID_SOCK) {
        return false;
    }
    if (bind(lst, (struct sockaddr *)&addr, alen) != 0 || listen(lst, 1) != 0) {
        goto fail;
    }
    if (getsockname(lst, (struct sockaddr *)&addr, &alen) != 0) {
        goto fail;
    }
    cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cli == NT_INVALID_SOCK) {
        goto fail;
    }
    (void)sock_set_int(cli, SO_RCVBUF, reader_rcvbuf); /* before connect: caps window + disables Win auto-tuning. */
    if (connect(cli, (struct sockaddr *)&addr, (nt_socklen_t)sizeof addr) != 0) {
        goto fail;
    }
    srv = accept(lst, NULL, NULL);
    if (srv == NT_INVALID_SOCK) {
        goto fail;
    }
    nt_close_sock(lst);
    set_nonblocking(srv);
    (void)sock_set_int(srv, SO_SNDBUF, writer_sndbuf);
    *w = srv;
    *r = cli;
    return true;
fail:
    if (cli != NT_INVALID_SOCK) {
        nt_close_sock(cli);
    }
    nt_close_sock(lst);
    return false;
}

/* Send until the OS itself reports the buffer full (EWOULDBLOCK), peer never draining — adapts to the
   real/auto-tuned buffer instead of guessing a size that wedges (Windows loopback ignores a small
   SO_RCVBUF). false if it never blocked within the cap. */
static bool fill_until_block(nt_sock_t s) {
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    static char chunk[65536];                             /* static: 64 KiB off the stack, zero-init. */
    for (size_t total = 0; total < ((size_t)64 << 20);) { /* 64 MiB cap: backpressure must show first. */
#ifdef _WIN32
        int n = send(s, chunk, (int)sizeof chunk, flags);
#else
        ssize_t n = send(s, chunk, sizeof chunk, flags);
#endif
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        return nt_sock_errno() == NT_EWOULDBLOCK;
    }
    return false;
}

/* See the header. 0 = pass; non-zero = the first failed step. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int nt_devapi_net_backpressure_selftest(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }
#endif
    int code = 0;
    nt_sock_t w = NT_INVALID_SOCK;
    nt_sock_t r = NT_INVALID_SOCK;
    char *send_buf = NULL;
    char *recv_buf = NULL;
    int rcv = 0;
    size_t plen = 0;
    size_t got = 0;

    /* Pair A — large receive window: the success path. 4 KB writer send buffer forces EWOULDBLOCK. */
    if (!loopback_pair(&w, &r, 4096, 1 << 20)) {
        code = 2;
        goto done;
    }

    /* Step 3: a fresh empty-buffer writer is immediately writable. */
    if (!sock_wait_writable(w, 200)) {
        code = 3;
        goto done;
    }

    /* Step 4-6: a large payload survives backpressure intact. The 4 KB SNDBUF forces EWOULDBLOCK ->
       exercises the select-wait recovery; the large RCVBUF lets the single-threaded reader drain it
       AFTER send_all returns (a healthy reader, even slow, must NOT be dropped). */
    rcv = sock_get_int(r, SO_RCVBUF);
    plen = (rcv > 8192) ? (size_t)(rcv / 4) : 4096U; /* /4: stay well inside the receive window. */
    send_buf = (char *)malloc(plen);
    recv_buf = (char *)malloc(plen);
    if (send_buf == NULL || recv_buf == NULL) {
        code = 4;
        goto done;
    }
    for (size_t i = 0; i < plen; i++) {
        send_buf[i] = (char)(i & 0x7F);
    }
    if (!send_all(w, send_buf, plen, 2000)) {
        code = 5;
        goto done;
    }
    while (got < plen) {
#ifdef _WIN32
        int n = recv(r, recv_buf + got, (int)(plen - got), 0);
#else
        ssize_t n = recv(r, recv_buf + got, plen - got, 0);
#endif
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    if (got != plen || memcmp(send_buf, recv_buf, plen) != 0) {
        code = 6;
        goto done;
    }
    nt_close_sock(w);
    w = NT_INVALID_SOCK;
    nt_close_sock(r);
    r = NT_INVALID_SOCK;

    /* Step 7-9: the wedge path. Fill the writer until the OS reports the buffer full, reader never
       draining; then send_all must TIME OUT and return false — a wedged peer is dropped, not spun on
       (a hang would instead trip the ctest timeout). Short 200 ms window keeps it fast. */
    if (!loopback_pair(&w, &r, 4096, 4096)) {
        code = 7;
        goto done;
    }
    if (!fill_until_block(w)) {
        code = 8; /* never reached backpressure within the cap -> cannot exercise the wedge here. */
        goto done;
    }
    {
        char probe[64] = {0};
        if (send_all(w, probe, sizeof probe, 200)) {
            code = 9; /* buffer full + reader idle, yet send_all reported success -> wedge not detected. */
            goto done;
        }
    }

done:
    free(send_buf);
    free(recv_buf);
    if (w != NT_INVALID_SOCK) {
        nt_close_sock(w);
    }
    if (r != NT_INVALID_SOCK) {
        nt_close_sock(r);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return code;
}
// #endregion
#endif /* NT_TEST_ACCESS */
