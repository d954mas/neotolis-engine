#include "devapi/nt_devapi.h"

#if NT_DEVAPI_ENABLED && !defined(__EMSCRIPTEN__)

#include <string.h>

#include "log/nt_log.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define TJ_BADSOCK INVALID_SOCKET
#define TJ_CLOSESOCK closesocket
static int sock_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void sock_set_nonblock(sock_t s) {
    u_long mode = 1;
    (void)ioctlsocket(s, (long)FIONBIO, &mode);
}
static int sock_recv(sock_t s, char *b, int n) { return recv(s, b, n, 0); }
static int sock_send(sock_t s, const char *b, int n) { return send(s, b, n, 0); }
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define TJ_BADSOCK (-1)
#define TJ_CLOSESOCK close
static int sock_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void sock_set_nonblock(sock_t s) {
    int f = fcntl(s, F_GETFL, 0);
    (void)fcntl(s, F_SETFL, f | O_NONBLOCK);
}
static int sock_recv(sock_t s, char *b, int n) { return (int)recv(s, b, (size_t)n, 0); }
static int sock_send(sock_t s, const char *b, int n) { return (int)send(s, b, (size_t)n, 0); }
#endif

static sock_t s_listen = TJ_BADSOCK;
static sock_t s_client = TJ_BADSOCK;
static char s_rx[2048];
static int s_rxlen;

static void close_client(void) {
    if (s_client != TJ_BADSOCK) {
        TJ_CLOSESOCK(s_client);
        s_client = TJ_BADSOCK;
    }
    s_rxlen = 0;
}

bool nt_devapi_net_start(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
#endif
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen == TJ_BADSOCK) {
        return false;
    }
    int yes = 1;
    (void)setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 only */

    if (bind(s_listen, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s_listen, 1) != 0) {
        TJ_CLOSESOCK(s_listen);
        s_listen = TJ_BADSOCK;
        return false;
    }
    sock_set_nonblock(s_listen);
    nt_log_info("devapi: TCP listening on 127.0.0.1:%u", (unsigned)port);
    return true;
}

static void handle_line(char *line) {
    static char resp[1 << 16];
    int n = nt_devapi_dispatch(line, resp, (int)sizeof(resp) - 1);
    resp[n] = '\n';
    (void)sock_send(s_client, resp, n + 1);
}

void nt_devapi_net_poll(void) {
    if (s_listen == TJ_BADSOCK) {
        return;
    }
    if (s_client == TJ_BADSOCK) {
        sock_t c = accept(s_listen, NULL, NULL);
        if (c == TJ_BADSOCK) {
            return;
        }
        sock_set_nonblock(c);
        s_client = c;
        s_rxlen = 0;
    }

    /* Drain whatever is buffered without blocking. */
    for (;;) {
        int space = (int)sizeof(s_rx) - 1 - s_rxlen;
        if (space <= 0) {
            s_rxlen = 0; /* overlong line: drop and resync */
            space = (int)sizeof(s_rx) - 1;
        }
        int n = sock_recv(s_client, s_rx + s_rxlen, space);
        if (n > 0) {
            s_rxlen += n;
        } else if (n == 0) {
            close_client();
            return;
        } else {
            if (!sock_would_block()) {
                close_client();
                return;
            }
            break;
        }
    }

    /* Dispatch every complete line; keep the trailing partial. */
    int start = 0;
    for (int i = 0; i < s_rxlen; i++) {
        if (s_rx[i] == '\n') {
            s_rx[i] = '\0';
            handle_line(s_rx + start);
            start = i + 1;
        }
    }
    if (start > 0) {
        memmove(s_rx, s_rx + start, (size_t)(s_rxlen - start));
        s_rxlen -= start;
    }
}

void nt_devapi_net_stop(void) {
    close_client();
    if (s_listen != TJ_BADSOCK) {
        TJ_CLOSESOCK(s_listen);
        s_listen = TJ_BADSOCK;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

#endif /* NT_DEVAPI_ENABLED && !__EMSCRIPTEN__ */
