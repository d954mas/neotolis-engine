// #region platform shim (D-02: this file IS the platform boundary)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_net.h"

/* Single loopback client; reconnect allowed (D-03). */
static nt_sock_t s_listen = NT_INVALID_SOCK;
static nt_sock_t s_client = NT_INVALID_SOCK;

/* Growing recv accumulation buffer; JSON-lines split on '\n' (D-05). Dev-only, no input cap.
   s_recv_len bytes are the unconsumed remainder carried between polls/frames. */
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
}

static void set_nonblocking(nt_sock_t s) {
#ifdef _WIN32
    u_long nb = 1;
    (void)ioctlsocket(s, (long)FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0);
    (void)fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

static void set_client_opts(nt_sock_t s) {
    int yes = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, (nt_socklen_t)sizeof yes);
#if defined(SO_NOSIGPIPE)
    (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&yes, (nt_socklen_t)sizeof yes); /* macOS/BSD (D-25) */
#endif
    set_nonblocking(s);
}
// #endregion

// #region send (partial-send safe, D-23/D-25)
/* Returns false on a real peer error so the caller drops the client. EWOULDBLOCK gets a short
   bounded retry — devapi payloads are tiny/low-frequency (D-08), never a forever busy-spin. */
static bool send_all(const char *p, size_t len) {
    size_t off = 0;
    int eagain_spins = 0;
    while (off < len) {
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL; /* suppress SIGPIPE on write-to-closed-peer (Linux, D-25). */
#endif
#ifdef _WIN32
        int chunk = (int)(len - off);
        int n = send(s_client, p + off, chunk, flags);
#else
        ssize_t n = send(s_client, p + off, len - off, flags);
#endif
        if (n > 0) {
            off += (size_t)n;
            eagain_spins = 0;
            continue;
        }
        int e = nt_sock_errno();
        if (e == NT_EWOULDBLOCK) {
            if (++eagain_spins > 10000) {
                return false; /* socket buffer wedged far past any sane dev payload — give up. */
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

/* Frame a response line: D-26 defence-in-depth — compact cJSON already escapes '\n' inside
   strings, so a raw '\n' before the framing newline would desync the client's line parser. */
static bool send_line(const char *resp) {
    size_t len = strlen(resp);
    NT_ASSERT(memchr(resp, '\n', len) == NULL);
    if (!send_all(resp, len)) {
        return false;
    }
    return send_all("\n", 1U);
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
#else
    signal(SIGPIPE, SIG_IGN); /* global fallback; MSG_NOSIGNAL/SO_NOSIGPIPE are the per-call guard (D-25). */
#endif

    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen == NT_INVALID_SOCK) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    int yes = 1;
#ifdef _WIN32
    /* SO_REUSEADDR is a port-hijack vector on Windows — use EXCLUSIVE (D-21). */
    (void)setsockopt(s_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&yes, (nt_socklen_t)sizeof yes);
#else
    /* SO_REUSEADDR on POSIX avoids TIME_WAIT "address in use" on rapid restart (D-21). */
    (void)setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &yes, (nt_socklen_t)sizeof yes);
#endif
    (void)setsockopt(s_listen, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, (nt_socklen_t)sizeof yes);
    set_nonblocking(s_listen);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 ONLY — never INADDR_ANY (XPORT-02). */

    if (bind(s_listen, (struct sockaddr *)&addr, (nt_socklen_t)sizeof addr) != 0 || listen(s_listen, 1) != 0) {
        nt_close_sock(s_listen);
        s_listen = NT_INVALID_SOCK;
#ifdef _WIN32
        WSACleanup();
#endif
        return false; /* port taken / refused — API-contract return, not an assert. */
    }
    return true;
}

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
    WSACleanup();
#endif
}
// #endregion

// #region accept / poll
/* Non-blocking accept; sets the new client non-blocking + TCP_NODELAY. */
static void try_accept(void) {
    nt_sock_t c = accept(s_listen, NULL, NULL);
    if (c != NT_INVALID_SOCK) {
        s_client = c;
        s_recv_len = 0;
        set_client_opts(c);
    }
    /* else EWOULDBLOCK == no pending connection (D-03) — try again next frame. */
}

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

    // #region recv (D-22 orderly close vs D-24 no-data)
    char tmp[4096];
    for (;;) {
#ifdef _WIN32
        int n = recv(s_client, tmp, (int)sizeof tmp, 0);
#else
        ssize_t n = recv(s_client, tmp, sizeof tmp, 0);
#endif
        if (n > 0) {
            recv_append(tmp, (size_t)n);
            continue;
        }
        if (n == 0) {
            close_client(); /* orderly disconnect (D-22) — distinct from EWOULDBLOCK. */
            return;
        }
        int e = nt_sock_errno();
        if (e == NT_EWOULDBLOCK) {
            break; /* no more data this frame (D-24). */
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

    // #region framed dispatch (D-05/D-07): submit each line, write its response inline
    /* Read cursor across the accumulated buffer; the remainder is shifted to the front after
       the line loop, so the returned submit/poll_response pointers are never held across a
       recv_append (Pitfall 4 / D-04). */
    size_t cur = 0;
    for (;;) {
        char *nl = (char *)memchr(s_recv_buf + cur, '\n', s_recv_len - cur);
        if (nl == NULL) {
            break;
        }
        *nl = '\0';
        char *line = s_recv_buf + cur;
        cur = (size_t)(nl - s_recv_buf) + 1U;

        const char *resp = nt_devapi_submit(line); /* Phase 63 core, unchanged for sync. */
        if (resp != NULL) {
            /* Write BEFORE the next core call — s_resp_buf is reused (D-07). */
            if (!send_line(resp)) {
                close_client();
                return;
            }
        }
        /* resp == NULL → deferred (D-06): nothing now; arrives via the drain below. */
    }
    /* Shift the unconsumed remainder to the front. */
    if (cur > 0) {
        size_t rest = s_recv_len - cur;
        if (rest > 0) {
            memmove(s_recv_buf, s_recv_buf + cur, rest);
        }
        s_recv_len = rest;
    }
    // #endregion

    // #region deferred drain (D-07/D-08): write every ready result, no per-poll cap
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

// #region wait_for_client (D-04 opt-in pre-loop gate, bounded)
static uint64_t now_ms(void) { return (uint64_t)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC); }

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
