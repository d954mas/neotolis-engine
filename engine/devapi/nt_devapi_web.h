#ifndef NT_DEVAPI_WEB_H
#define NT_DEVAPI_WEB_H

/* Web (Emscripten) transport for the devapi core: a push/pull ccall bridge instead of a socket.
   JS pushes a request line via an EMSCRIPTEN_KEEPALIVE export and pulls queued responses; the core
   stays transport-agnostic (it owns nt_devapi_update + the frame-keyed deferred drain). Web-only
   (compiled out of the native build); dev-only, gated by NT_DEVAPI_ENABLED with the rest of the
   layer. The host calls only nt_devapi_web_install_shim once at startup; the two KEEPALIVE wrappers
   below are reached from JS through window.__devapi (no socket / no listener). */

/* Must come from the nt_devapi target compile-defines; consumers without the link see stubs that mismatch ABI. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

/* Install window.__devapi = {submit, poll, tick, ready}. The host calls this once on the web path
   (after nt_devapi_init + group registration); it is the only web entry the host names. */
void nt_devapi_web_install_shim(void);

/* JS-facing transport wrappers (reached via window.__devapi, not by the host directly). They return
   the SAME const char* the core owns, NULL when deferred / nothing ready. */
const char *nt_devapi_web_submit(const char *line);
const char *nt_devapi_web_poll(void);

#endif /* NT_DEVAPI_WEB_H */
