#ifndef NT_DEVAPI_NET_H
#define NT_DEVAPI_NET_H

/* Native loopback TCP transport for the devapi core. The sole socket boundary:
   all platform #ifdef _WIN32 (Winsock) vs POSIX code lives in nt_devapi_net.c.
   Native-only (compiled out of the Emscripten/web build); dev-only, gated by
   NT_DEVAPI_ENABLED with the rest of the layer. */

/* Must come from the nt_devapi target compile-defines: the header is meaningless without the link. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

#include <stdbool.h>
#include <stdint.h>

/* Listen port when the NT_DEVAPI_PORT env var is unset or empty. */
#ifndef NT_DEVAPI_DEFAULT_PORT
#define NT_DEVAPI_DEFAULT_PORT 17890
#endif

/* Start the loopback (127.0.0.1 ONLY) TCP server: single client (the newest connection replaces it),
   JSON-lines, non-blocking. The port comes from the NT_DEVAPI_PORT env var (unset/empty ->
   NT_DEVAPI_DEFAULT_PORT), so each process can get its own.
   Returns false (and logs why) on a bad NT_DEVAPI_PORT value or a socket/bind/listen failure — the port
   may legitimately be taken, so this is an API-contract return, NOT an assert. Call nt_devapi_init
   first; pair with _stop. */
bool nt_devapi_net_start(void);

/* Close the client + listen sockets and tear down platform networking (WSACleanup on Windows). */
void nt_devapi_net_stop(void);

/* Opt-in pre-loop gate: blocks in select() on the listen socket until a client connects or timeout_ms
   elapses (bounded, not busy-spun). Returns true if a client is connected. The game wires this BEFORE
   its loop when the bot must push setup before frame 0; normal per-frame accept continues after it returns. */
bool nt_devapi_net_wait_for_client(uint32_t timeout_ms);

#endif /* NT_DEVAPI_NET_H */
