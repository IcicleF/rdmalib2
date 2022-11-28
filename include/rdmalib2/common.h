#pragma once

#ifndef __RDMALIB2_COMMON_H__
#define __RDMALIB2_COMMON_H__

#include <cstdint>
#include <cstdlib>
#include <errno.h>
#include <unistd.h>

namespace rdmalib2 {

[[noreturn]] static inline void panic() { exit(EXIT_FAILURE); }

[[noreturn]] static inline void panic_with_errno() { exit(errno); }

[[noreturn]] static inline void panic_with_errno(int _errno) { exit(_errno); }

static void *add_void_ptr(void *ptr, size_t size) {
    return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(ptr) + size);
}

} // namespace rdmalib2

#endif // __RDMALIB2_COMMON_H__
