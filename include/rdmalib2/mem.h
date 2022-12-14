#pragma once

#ifndef __RDMALIB2_MEM_H__
#define __RDMALIB2_MEM_H__

#include "context.h"
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>

namespace rdmalib2 {

class rdma_memory_slice;

class rdma_memory_region {
protected:
    static constexpr uint32_t Host = 0, Device = 1;

    template <uint32_t Type> struct memory_region_type_base {};
    template <uint32_t Perm> struct memory_region_perm_base {
        template <uint32_t Perm2>
        constexpr memory_region_perm_base<Perm | Perm2>
        operator+(memory_region_perm_base<Perm2>) const {
            return {};
        }
    };

public:
    template <uint32_t T, uint32_t P>
    rdma_memory_region(rdma_context const &ctx,
                       memory_region_type_base<T> const &type,
                       memory_region_perm_base<P> const &perm, void *ptr,
                       size_t size)
        : ctx(ctx), ptr(ptr), size(size) {
        auto mrdm = create_rdma_memory_region(ctx, type, perm, ptr, size);
        if (mrdm.has_value()) {
            this->mr = std::get<0>(mrdm.value());
            this->dm = std::get<1>(mrdm.value());
            if constexpr (T == Host) {
                spdlog::trace(
                    "created host memory region {:p} on address [{:p}, {:p})",
                    reinterpret_cast<void *>(this->mr), ptr,
                    add_void_ptr(ptr, size));
            } else {
                spdlog::trace("created device memory region {:p} on dm {:p} of "
                              "length {} with start address {:p}",
                              reinterpret_cast<void *>(this->mr),
                              reinterpret_cast<void *>(this->dm), size, ptr);
            }
        } else {
            spdlog::error("failed to create memory region for {} memory, "
                          "permission {}, on address [{:p}, {:p})",
                          (T == Host ? "host" : "device"), P, ptr,
                          add_void_ptr(ptr, size));
            panic_with_errno();
        }
    }

    rdma_memory_region(rdma_context const &ctx, void *ptr, size_t size)
        : rdma_memory_region(ctx, host_memory{}, full_perm{}, ptr, size) {}

    template <uint32_t T, uint32_t P>
    rdma_memory_region(rdma_context const &ctx,
                       memory_region_type_base<T> const &type, size_t size)
        : rdma_memory_region(ctx, type, full_perm{}, nullptr, size) {
        static_assert(
            std::is_same_v<memory_region_type_base<T>, device_memory>,
            "cannot create host memory region without specifying its pointer");
    }

    template <uint32_t T, uint32_t P>
    rdma_memory_region(rdma_context const &ctx,
                       memory_region_type_base<T> const &type, void *ptr,
                       size_t size)
        : rdma_memory_region(ctx, type, full_perm{}, ptr, size) {}

    rdma_memory_region(rdma_memory_region const &) = delete;
    rdma_memory_region &operator=(rdma_memory_region const &) = delete;

    rdma_memory_region(rdma_memory_region &&other) noexcept
        : ctx(other.ctx),
          mr(other.mr),
          dm(other.dm),
          ptr(other.ptr),
          size(other.size) {
        other.mr = nullptr;
        other.dm = nullptr;
        other.ptr = nullptr;
        other.size = 0;
    }

    rdma_memory_region &operator=(rdma_memory_region &&other) & noexcept {
        if (this != &other) {
            this->~rdma_memory_region();
            new (this) rdma_memory_region(std::move(other));
        }
        return *this;
    }

    ~rdma_memory_region() {
        if (mr) {
            spdlog::trace("destroying memory region {:p}",
                          reinterpret_cast<void *>(mr));
            ibv_dereg_mr(mr);
            mr = nullptr;
        }
        if (dm) {
            spdlog::trace("destroying device memory {:p}",
                          reinterpret_cast<void *>(dm));
            ibv_exp_free_dm(dm);
            dm = nullptr;
        }

        ptr = nullptr;
        size = 0;
    }

    rdma_context const &get_context() const { return ctx; }
    ibv_mr *get_mr() const { return mr; }
    uint32_t get_lkey() const { return mr->lkey; }
    uint32_t get_rkey() const { return mr->rkey; }
    void *get_ptr() const { return ptr; }
    size_t get_size() const { return size; }

    rdma_memory_slice slice(size_t offset, size_t size) const;
    rdma_memory_slice slice(size_t offset = 0) const;

public:
    typedef memory_region_type_base<Host> host_memory;
    typedef memory_region_type_base<Device> device_memory;

    typedef memory_region_perm_base<0> read_only;
    typedef memory_region_perm_base<IBV_ACCESS_LOCAL_WRITE> read_write;
    typedef memory_region_perm_base<IBV_ACCESS_REMOTE_READ |
                                    IBV_ACCESS_REMOTE_WRITE |
                                    IBV_ACCESS_REMOTE_ATOMIC>
        remote_read_write;
    typedef memory_region_perm_base<
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC>
        full_perm;

protected:
    template <uint32_t Type, uint32_t Perm>
    static std::optional<std::tuple<ibv_mr *, ibv_exp_dm *>>
    create_rdma_memory_region(rdma_context const &ctx,
                              memory_region_type_base<Type> const &,
                              memory_region_perm_base<Perm> const &, void *ptr,
                              size_t size) {
        if constexpr (Type == Host) {
            ibv_mr *mr = ibv_reg_mr(ctx.get_pd(), ptr, size, Perm);
            return mr ? std::make_optional(std::make_tuple(mr, nullptr))
                      : std::nullopt;
        } else {
            // Allocate device memory
            ibv_exp_alloc_dm_attr dm_attr = {};
            dm_attr.length = size;

            ibv_exp_dm *dm = ibv_exp_alloc_dm(ctx.get_context(), &dm_attr);
            if (!dm) {
                return std::nullopt;
            }

            // Register device memory
            ibv_exp_reg_mr_in reg_mr_in = {};
            reg_mr_in.pd = ctx.get_pd();
            reg_mr_in.addr = ptr;
            reg_mr_in.length = size;
            reg_mr_in.exp_access = Perm;
            reg_mr_in.dm = dm;
            reg_mr_in.comp_mask = IBV_EXP_REG_MR_DM;

            ibv_mr *mr = ibv_exp_reg_mr(&reg_mr_in);
            if (!mr) {
                ibv_exp_free_dm(dm);
                return std::nullopt;
            }

            // Zero device memory to make it initialized
            char *zero_buf = new char[size];
            memset(zero_buf, 0, size);

            ibv_exp_memcpy_dm_attr memcpy_dm_attr = {};
            memcpy_dm_attr.memcpy_dir = IBV_EXP_DM_CPY_TO_DEVICE;
            memcpy_dm_attr.host_addr = zero_buf;
            memcpy_dm_attr.dm_offset = 0;
            memcpy_dm_attr.length = size;
            ibv_exp_memcpy_dm(dm, &memcpy_dm_attr);

            delete[] zero_buf;
            return std::make_optional(std::make_tuple(mr, dm));
        }
    }

    rdma_context const &ctx;
    ibv_mr *mr = nullptr;
    ibv_exp_dm *dm = nullptr;

    void *ptr = nullptr;
    size_t size = 0;
};

class rdma_memory_slice {
    friend class rdma_memory_region;

public:
    rdma_memory_slice(rdma_memory_region const &region, size_t offset,
                      size_t size)
        : region(region),
          ptr(add_void_ptr(region.get_ptr(), offset)),
          size(size) {}

    rdma_memory_slice(rdma_memory_region const &region)
        : rdma_memory_slice(region, 0, region.get_size()) {}

    ~rdma_memory_slice() = default;

    //! \brief Gets the pointer to the start position of the memory slice.
    void *get_ptr() const { return ptr; }

    //! \brief Gets the size of the memory slice.
    size_t get_size() const { return size; }

    //! \brief Gets the memory region that the memory slice belongs to.
    rdma_memory_region const &get_region() const { return region; }

    //! \brief Gets the raw ibv_mr instance of the memory region that the memory
    //! slice belongs to.
    ibv_mr *get_raw_mr() const { return get_region().get_mr(); }

    //! \brief Gets the local key of the memory region that the memory slice
    //! belongs to.
    uint32_t get_lkey() const { return get_region().get_lkey(); }

    //! \brief Gets the remote key of the memory region that the memory slice
    //! belongs to.
    uint32_t get_rkey() const { return get_region().get_rkey(); }

    //! \brief Extracts a sub-slice of the memory slice.
    rdma_memory_slice slice(size_t offset, size_t size) const {
        if (offset + size > this->size) {
            spdlog::error("desired memory slice [{}, {}) is larger than the "
                          "parent slice (size {})",
                          offset, offset + size, this->size);
            panic();
        }
        return rdma_memory_slice{region, offset, size};
    }

    rdma_memory_slice slice(size_t offset = 0) const {
        return slice(offset, size - offset);
    }

    template <typename T> T &as() const {
        if (sizeof(T) != size) {
            spdlog::warn("size mismatch when casting: type {} != slice {}",
                         sizeof(T), size);
        }
        return *reinterpret_cast<T *>(ptr);
    }

    template <typename T>
    std::enable_if_t<std::is_pointer_v<T>, T> as_ptr() const {
        return reinterpret_cast<T>(ptr);
    }

    ibv_sge to_sge() const {
        if (unlikely(size > std::numeric_limits<uint32_t>::max())) {
            spdlog::error("memory slice size {} is larger than the maximum "
                          "allowed size {}",
                          size, std::numeric_limits<uint32_t>::max());
            panic();
        }
        return {.addr = reinterpret_cast<uint64_t>(ptr),
                .length = static_cast<uint32_t>(size),
                .lkey = get_lkey()};
    }

    bool is_aligned(size_t alignment = 8) const {
        return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
    }

    rdma_memory_region const &region;
    void *ptr;
    size_t size;
};

inline rdma_memory_slice rdma_memory_region::slice(size_t offset,
                                                   size_t size) const {
    if (unlikely(offset + size > this->size)) {
        spdlog::error("desired memory slice [{}, {}) is larger than the "
                      "region (size {})",
                      offset, offset + size, this->size);
        panic();
    }
    return rdma_memory_slice{*this, offset, size};
}

inline rdma_memory_slice rdma_memory_region::slice(size_t offset) const {
    return slice(offset, size - offset);
}

class rdma_remote_memory_slice {
public:
    rdma_remote_memory_slice(uint64_t addr, uint64_t size, uint32_t rkey)
        : addr(addr), size(size), rkey(rkey) {}

    rdma_remote_memory_slice(rdma_remote_memory_slice const &) = default;
    rdma_remote_memory_slice &
    operator=(rdma_remote_memory_slice const &) = default;

    rdma_remote_memory_slice(rdma_remote_memory_slice &&) = default;
    rdma_remote_memory_slice &operator=(rdma_remote_memory_slice &&) = default;

    ~rdma_remote_memory_slice() = default;

    uint64_t get_addr() const { return addr; }
    uint64_t get_size() const { return size; }
    uint32_t get_rkey() const { return rkey; }

protected:
    uint64_t addr;
    uint64_t size;
    uint64_t rkey;
};

} // namespace rdmalib2

#endif
