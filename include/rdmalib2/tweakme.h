#pragma once

#ifndef __RDMALIB2_TWEAKME_H__
#define __RDMALIB2_TWEAKME_H__

#include <cstdint>

namespace rdmalib2 {

static constexpr uint16_t kRpcPort = 8392;

static constexpr int kQpDepth = 256;
static constexpr int kCqDepth = 256;
static constexpr uint32_t kMaxSge = 16;
static constexpr uint32_t kMaxInlineData = 64;

} // namespace rdmalib2

#endif // __RDMALIB2_TWEAKME_H__
