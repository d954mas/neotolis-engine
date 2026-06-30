#ifndef NT_DEVAPI_WEB_H
#define NT_DEVAPI_WEB_H

/* Web (Emscripten) transport for the devapi core: a push/pull ccall bridge instead of a socket.
   JS pushes a request line via an EMSCRIPTEN_KEEPALIVE export and pulls queued responses; the core
   stays transport-agnostic (it owns nt_devapi_update + the frame-keyed deferred drain). Web-only
   (compiled out of the native build); dev-only, gated by NT_DEVAPI_ENABLED with the rest of the
   layer. The real export surface is fleshed out in Plan 02 — this is the placeholder the D-18 CMake
   branch references so the web target is real and CMake-valid now. */

/* Must come from the nt_devapi target compile-defines; consumers without the link see stubs that mismatch ABI. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

#endif /* NT_DEVAPI_WEB_H */
