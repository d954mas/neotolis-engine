# nt_clipboard -- sync clipboard façade with swappable backends (#126, D-07/D-08)
# API header: nt_clipboard.h (shared by all implementations)
# Real impl:  native/nt_clipboard_native.c (glfwGet/SetClipboardString)
#             web/nt_clipboard_web.c       (cache-on-paste EM_JS, async contained)
# Stub impl:  stub/nt_clipboard_stub.c     (get -> "", set -> noop)
# Interface:  nt_clipboard_interface (header-only; nt_ui links this, exe picks an impl)

# Header-only interface carries nt_clipboard.h's include root; the exe selects the impl.
nt_declare_interface(nt_clipboard)

if(EMSCRIPTEN)
    nt_add_module(nt_clipboard LOG_DOMAIN "clipboard" web/nt_clipboard_web.c nt_clipboard.h)
    # Web backend bridges the DOM paste string into C: stringToNewUTF8 (runtime
    # method) allocates it, _free releases it. INTERFACE so any exe linking
    # nt_clipboard inherits the exports.
    target_link_options(nt_clipboard INTERFACE
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_free']"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['stringToNewUTF8','UTF8ToString']"
    )
    target_link_libraries(nt_clipboard PUBLIC nt_core nt_clipboard_interface)
else()
    nt_add_module(nt_clipboard LOG_DOMAIN "clipboard" native/nt_clipboard_native.c nt_clipboard.h)
    # nt_window_interface carries glfw's include path on native (the fixed windowing
    # dep); glfw symbols resolve at exe link via the real nt_window.
    target_link_libraries(nt_clipboard PUBLIC nt_core nt_clipboard_interface nt_window_interface)
endif()

nt_add_module(nt_clipboard_stub LOG_DOMAIN "clipboard_stub" stub/nt_clipboard_stub.c nt_clipboard.h)
target_link_libraries(nt_clipboard_stub PUBLIC nt_core nt_clipboard_interface)
