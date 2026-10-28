/* Loopback transport: a large response survives a full send buffer (EWOULDBLOCK -> select-wait ->
 * delivered intact), a wedged peer is dropped on a bounded timeout, never busy-spun, and the pre-loop
 * wait_for_client gate is bounded. The TU includes nt_devapi_net.c to reach its static socket helpers,
 * so it reuses that file's platform shim. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
// NOLINTNEXTLINE(bugprone-suspicious-include): drives the static send_all / net_listen directly.
#include "devapi/nt_devapi_net.c"
#include "unity.h"
/* clang-format on */

#ifdef _WIN32
static bool s_wsa_test = false;
#endif

void setUp(void) {
#ifdef _WIN32
    WSADATA wsa;
    s_wsa_test = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    TEST_ASSERT_TRUE(s_wsa_test);
#endif
}

void tearDown(void) {
    if (s_listen != NT_INVALID_SOCK) { /* a failed assert skipped the test's own stop */
        nt_devapi_net_stop();
        nt_devapi_shutdown();
    }
#ifdef _WIN32
    if (s_wsa_test) {
        WSACleanup();
        s_wsa_test = false;
    }
#endif
}

// #region loopback helpers
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
// #endregion

/* A large payload survives backpressure intact. The 4 KB SNDBUF forces EWOULDBLOCK -> exercises the
   select-wait recovery; the large RCVBUF lets the single-threaded reader drain it AFTER send_all returns
   (a healthy reader, even slow, must NOT be dropped). */
void test_large_payload_survives_backpressure(void) {
    nt_sock_t w = NT_INVALID_SOCK;
    nt_sock_t r = NT_INVALID_SOCK;
    TEST_ASSERT_TRUE(loopback_pair(&w, &r, 4096, 1 << 20));
    TEST_ASSERT_TRUE_MESSAGE(sock_wait_writable(w, 200), "a fresh empty-buffer writer must be writable");

    int rcv = sock_get_int(r, SO_RCVBUF);
    size_t plen = (rcv > 8192) ? (size_t)(rcv / 4) : 4096U; /* /4: stay well inside the receive window. */
    char *send_buf = (char *)malloc(plen);
    char *recv_buf = (char *)malloc(plen);
    TEST_ASSERT_NOT_NULL(send_buf);
    TEST_ASSERT_NOT_NULL(recv_buf);
    for (size_t i = 0; i < plen; i++) {
        send_buf[i] = (char)(i & 0x7F);
    }
    bool sent = send_all(w, send_buf, plen, 2000);
    size_t got = 0;
    while (sent && got < plen) {
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
    bool same = got == plen && memcmp(send_buf, recv_buf, plen) == 0;
    free(send_buf);
    free(recv_buf);
    nt_close_sock(w);
    nt_close_sock(r);
    TEST_ASSERT_TRUE_MESSAGE(sent, "send_all gave up on a healthy reader");
    TEST_ASSERT_TRUE_MESSAGE(same, "payload arrived short or corrupted");
}

/* The wedge path: buffer full, reader never draining -> send_all must TIME OUT and return false (a hang
   would instead trip the ctest timeout). Short 200 ms window keeps it fast. */
void test_wedged_peer_times_out(void) {
    nt_sock_t w = NT_INVALID_SOCK;
    nt_sock_t r = NT_INVALID_SOCK;
    TEST_ASSERT_TRUE(loopback_pair(&w, &r, 4096, 4096));
    bool blocked = fill_until_block(w);
    char probe[64] = {0};
    bool sent = blocked && send_all(w, probe, sizeof probe, 200);
    nt_close_sock(w);
    nt_close_sock(r);
    TEST_ASSERT_TRUE_MESSAGE(blocked, "never reached backpressure within the cap");
    TEST_ASSERT_FALSE_MESSAGE(sent, "buffer full + reader idle, yet send_all reported success");
}

// #region wait_for_client
/* Listens on an ephemeral port (no clash with a running host) and returns it. */
static uint16_t listen_ephemeral(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    TEST_ASSERT_TRUE(net_listen(0));
    struct sockaddr_in addr;
    nt_socklen_t alen = (nt_socklen_t)sizeof addr;
    TEST_ASSERT_EQUAL_INT(0, getsockname(s_listen, (struct sockaddr *)&addr, &alen));
    return ntohs(addr.sin_port);
}

void test_wait_for_client_times_out_without_client(void) {
    (void)listen_ephemeral();
    TEST_ASSERT_FALSE(nt_devapi_net_wait_for_client(50));
}

void test_wait_for_client_returns_on_connect(void) {
    uint16_t port = listen_ephemeral();
    nt_sock_t cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    TEST_ASSERT_TRUE(cli != NT_INVALID_SOCK);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    int rc = connect(cli, (struct sockaddr *)&addr, (nt_socklen_t)sizeof addr);
    bool connected = rc == 0 && nt_devapi_net_wait_for_client(2000);
    nt_close_sock(cli);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(connected);
}
// #endregion

static void set_port_env(const char *value) {
#ifdef _WIN32
    TEST_ASSERT_EQUAL_INT(0, _putenv_s("NT_DEVAPI_PORT", value)); /* "" removes it */
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test
    TEST_ASSERT_EQUAL_INT(0, value[0] ? setenv("NT_DEVAPI_PORT", value, 1) : unsetenv("NT_DEVAPI_PORT"));
#endif
}

/* A port free right now: a value built from it can only be refused by the parse, never by bind. */
static uint16_t free_port(void) {
    nt_sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    TEST_ASSERT_TRUE(s != NT_INVALID_SOCK);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    nt_socklen_t alen = (nt_socklen_t)sizeof addr;
    int rc = bind(s, (struct sockaddr *)&addr, alen);
    rc = rc == 0 ? getsockname(s, (struct sockaddr *)&addr, &alen) : rc;
    nt_close_sock(s);
    TEST_ASSERT_EQUAL_INT(0, rc);
    return ntohs(addr.sin_port);
}

/* A value that is not a port in 1..65535 refuses to start (no fallback to the default port). */
void test_start_rejects_a_bad_port_env(void) {
    char junk[16];
    (void)snprintf(junk, sizeof junk, "%ux", (unsigned)free_port());
    const char *const bad[] = {"abc", "0", "65536", junk, "-1"};
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    bool started = false;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0] && !started; i++) {
        set_port_env(bad[i]);
        started = nt_devapi_net_start();
    }
    set_port_env("");
    nt_devapi_shutdown();
    TEST_ASSERT_FALSE(started);
}

void test_start_listens_on_the_env_port(void) {
    uint16_t port = free_port();
    char value[8];
    (void)snprintf(value, sizeof value, "%u", (unsigned)port);
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    set_port_env(value);
    bool started = nt_devapi_net_start();
    set_port_env("");
    TEST_ASSERT_TRUE(started);
    struct sockaddr_in addr;
    nt_socklen_t alen = (nt_socklen_t)sizeof addr;
    TEST_ASSERT_EQUAL_INT(0, getsockname(s_listen, (struct sockaddr *)&addr, &alen));
    TEST_ASSERT_EQUAL_UINT16(port, ntohs(addr.sin_port));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_large_payload_survives_backpressure);
    RUN_TEST(test_wedged_peer_times_out);
    RUN_TEST(test_wait_for_client_times_out_without_client);
    RUN_TEST(test_wait_for_client_returns_on_connect);
    RUN_TEST(test_start_rejects_a_bad_port_env);
    RUN_TEST(test_start_listens_on_the_env_port);
    return UNITY_END();
}
