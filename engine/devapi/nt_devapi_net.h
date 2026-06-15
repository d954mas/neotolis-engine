#ifndef NT_DEVAPI_NET_H
#define NT_DEVAPI_NET_H

/* Native loopback TCP transport for the devapi core. The sole socket boundary:
   all platform #ifdef _WIN32 (Winsock) vs POSIX code lives in nt_devapi_net.c.
   Native-only (compiled out of the Emscripten/web build); dev-only, gated by
   NT_DEVAPI_ENABLED with the rest of the layer. */

/* Must come from the nt_devapi target compile-defines; consumers without the link see stubs that mismatch ABI. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

#include <stdbool.h>
#include <stdint.h>

/* Default listen port. Override env var: NT_DEVAPI_PORT (read by the host, Plan 03). */
#ifndef NT_DEVAPI_DEFAULT_PORT
#define NT_DEVAPI_DEFAULT_PORT 17890
#endif

/* Start the loopback (127.0.0.1 ONLY) TCP server: single client, JSON-lines, non-blocking.
   Returns false on socket/bind/listen failure — the port may legitimately be taken, so this
   is an API-contract return, NOT an assert. Call nt_devapi_init first; pair with _stop. */
bool nt_devapi_net_start(uint16_t port);

/* Poll the transport once per frame: non-blocking accept + recv + framed dispatch + send +
   deferred drain. Never blocks the loop. Safe to call before a client connects. */
void nt_devapi_net_poll(void);

/* Close the client + listen sockets and tear down platform networking (WSACleanup on Windows). */
void nt_devapi_net_stop(void);

/* Opt-in pre-loop gate: bounded-busy non-blocking accept until a client connects or timeout_ms
   elapses. Returns true if a client is connected. The game wires this BEFORE its loop when the
   bot must push setup before frame 0 (D-04); normal per-frame accept continues after it returns. */
bool nt_devapi_net_wait_for_client(uint32_t timeout_ms);

#endif /* NT_DEVAPI_NET_H */
