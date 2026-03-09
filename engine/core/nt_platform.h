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
#define NT_ENABLE_ASSERTS 1
#endif

#endif /* NT_PLATFORM_H */
