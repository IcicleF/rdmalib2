#pragma once

#ifndef __RDMALIB2_COMMON_H__
#define __RDMALIB2_COMMON_H__

#include <cstdint>
#include <cstdlib>
#include <errno.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

namespace rdmalib2 {

[[noreturn]] static inline void panic() { exit(EXIT_FAILURE); }

[[noreturn]] static inline void panic_with_errno() { exit(errno); }

[[noreturn]] static inline void panic_with_errno(int _errno) { exit(_errno); }

static void *add_void_ptr(void *ptr, size_t size) {
    return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(ptr) + size);
}

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define RDMALIB2_ASSERT(cond)                                                  \
    do {                                                                       \
        if (unlikely(!(cond))) {                                               \
            spdlog::error("assertion failed: {}", #cond);                      \
            panic();                                                           \
        }                                                                      \
    } while (0)

#define RDMALIB2_ASSERT_WITH_ERRNO(cond)                                       \
    do {                                                                       \
        if (unlikely(!(cond))) {                                               \
            spdlog::error("assertion failed with errno {}: {}", errno, #cond); \
            panic_with_errno();                                                \
        }                                                                      \
    } while (0)

} // namespace rdmalib2

#include "tweakme.h"

#endif // __RDMALIB2_COMMON_H__
