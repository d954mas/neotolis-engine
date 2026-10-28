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

/* The conventional listen port: the fallback a host passes to nt_devapi_net_port_from_env. */
#ifndef NT_DEVAPI_DEFAULT_PORT
#define NT_DEVAPI_DEFAULT_PORT 17890
#endif

/* Start the loopback (127.0.0.1 ONLY) TCP server on `port`: single client (the newest connection
   replaces it), JSON-lines, non-blocking. The host picks the port (a fixed value, its CLI, or
   nt_devapi_net_port_from_env), so each process can get its own.
   Returns false (and logs why) on a socket/bind/listen failure — the port may legitimately be taken,
   so this is an API-contract return, NOT an assert. Call nt_devapi_init first; pair with _stop. */
bool nt_devapi_net_start(uint16_t port);

/* Read the NT_DEVAPI_PORT env var into *port: `fallback` when it is unset or empty. Returns false (and
   logs why) on a value that is not a port in 1..65535; *port is then unchanged. */
bool nt_devapi_net_port_from_env(uint16_t fallback, uint16_t *port);

/* Close the client + listen sockets and tear down platform networking (WSACleanup on Windows). */
void nt_devapi_net_stop(void);

/* Opt-in pre-loop gate: blocks in select() on the listen socket until a client connects or timeout_ms
   elapses (bounded, not busy-spun). Returns true if a client is connected. The game wires this BEFORE
   its loop when the bot must push setup before frame 0; normal per-frame accept continues after it returns. */
bool nt_devapi_net_wait_for_client(uint32_t timeout_ms);

#endif /* NT_DEVAPI_NET_H */
