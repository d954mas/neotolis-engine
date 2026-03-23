#ifndef NT_PLATFORM_H
#define NT_PLATFORM_H

/* Platform detection -- maps compiler defines to engine defines */
#if defined(__EMSCRIPTEN__)
#define NT_PLATFORM_WEB 1
#elif defined(_WIN32)
#define NT_PLATFORM_WIN 1
#else
/* Linux, macOS, other POSIX -- native builds for CI and development */
#define NT_PLATFORM_NATIVE 1
#endif

/* Build configuration -- maps standard defines to engine defines */
#ifdef NDEBUG
#define NT_RELEASE 1
#else
#define NT_DEBUG 1
#endif

/* Assert mode: 0=off, 1=trap, 2=full (handler+strings).
   CMake can override via -DNT_ASSERT_MODE=<level>. */
#ifndef NT_ASSERT_MODE
#ifdef NT_DEBUG
#define NT_ASSERT_MODE 2 /* full: handler available for tests, trap fallback */
#else
#define NT_ASSERT_MODE 0 /* release: off */
#endif
#endif

#endif /* NT_PLATFORM_H */
