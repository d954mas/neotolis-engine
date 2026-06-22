# nt_clipboard -- sync clipboard façade with swappable backends
# API header: nt_clipboard.h (shared by all implementations)
# Real impl:  native/nt_clipboard_native.c (glfwGet/SetClipboardString)
#             web/nt_clipboard_web.c       (cache-on-paste EM_JS, async contained)
# Stub impl:  stub/nt_clipboard_stub.c     (get -> "", set -> noop)
# Interface:  nt_clipboard_interface (header-only; nt_ui links this, exe picks an impl)

# Header-only interface carries nt_clipboard.h's include root; the exe selects the impl.
nt_declare_interface(nt_clipboard)

if(EMSCRIPTEN)
    nt_add_module(nt_clipboard LOG_DOMAIN "clipboard" web/nt_clipboard_web.c nt_clipboard.h)
    # Web backend self-declares its EM_JS runtime-method deps via EM_JS_DEPS in
    # nt_clipboard_web.c ($stringToNewUTF8,$UTF8ToString,_malloc,_free) -- composes
    # across modules and survives Closure, so no exe-level export list is needed.
    target_link_libraries(nt_clipboard PUBLIC nt_core nt_clipboard_interface)
else()
    nt_add_module(nt_clipboard LOG_DOMAIN "clipboard" native/nt_clipboard_native.c nt_clipboard.h)
    # nt_window_interface carries glfw's include path on native (the fixed windowing dep); glfw
    # symbols resolve at exe link via the real nt_window. So real native nt_clipboard REQUIRES the
    # exe to link the real nt_window -- nt_clipboard + nt_window_stub is not a valid pairing (the
    # glfw clipboard symbols stay unresolved: a loud link error, by design).
    target_link_libraries(nt_clipboard PUBLIC nt_core nt_clipboard_interface nt_window_interface)
endif()

nt_add_module(nt_clipboard_stub LOG_DOMAIN "clipboard_stub" stub/nt_clipboard_stub.c nt_clipboard.h)
target_link_libraries(nt_clipboard_stub PUBLIC nt_core nt_clipboard_interface)
