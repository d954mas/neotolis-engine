#pragma once
#include <stdexcept>
#define META_CHECK_EQUAL_DESCR(a, b, ...)                                                                                                                                                              \
    do {                                                                                                                                                                                               \
        if ((a) != (b))                                                                                                                                                                                \
            throw std::runtime_error("reference equality check");                                                                                                                                      \
    } while (false)
#define META_CHECK_LESS_DESCR(a, b, ...)                                                                                                                                                               \
    do {                                                                                                                                                                                               \
        if (!((a) < (b)))                                                                                                                                                                              \
            throw std::runtime_error("reference range check");                                                                                                                                         \
    } while (false)
#define META_CHECK_FALSE_DESCR(a, ...)                                                                                                                                                                 \
    do {                                                                                                                                                                                               \
        if (a)                                                                                                                                                                                         \
            throw std::runtime_error("reference false check");                                                                                                                                         \
    } while (false)
#define META_CHECK_DESCR(a, b, ...)                                                                                                                                                                    \
    do {                                                                                                                                                                                               \
        if (!(b))                                                                                                                                                                                      \
            throw std::runtime_error("reference invariant check");                                                                                                                                     \
    } while (false)

#define META_CHECK_NAME_DESCR(a, b, ...)                                                                                                                                                               \
    do {                                                                                                                                                                                               \
        if (!(b))                                                                                                                                                                                      \
            throw std::runtime_error("reference named check");                                                                                                                                         \
    } while (false)
#define META_CHECK_GREATER_OR_EQUAL_DESCR(a, b, ...)                                                                                                                                                   \
    do {                                                                                                                                                                                               \
        if (!((a) >= (b)))                                                                                                                                                                             \
            throw std::runtime_error("reference comparison");                                                                                                                                          \
    } while (false)
#define META_UNEXPECTED(...) throw std::runtime_error("reference unexpected value")
