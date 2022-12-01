#pragma once

#ifndef __RDMALIB2_PREDECLARE_VERB_H__
#define __RDMALIB2_PREDECLARE_VERB_H__

#include "../context.h"
#include "../mem.h"
#include <new>
#include <optional>

namespace rdmalib2 {

// Predeclaration of rdma_qp class
template <ibv_qp_type Type> class rdma_qp;

template <typename Wr> class rdma_verb {
protected:
    template <ibv_exp_wr_opcode Opcode> struct wr_type_base {
        static constexpr ibv_exp_wr_opcode opcode = Opcode;
    };

    using self_if_send =
        std::enable_if_t<std::is_same_v<Wr, ibv_exp_send_wr>, rdma_verb &>;
    using self_if_recv =
        std::enable_if_t<std::is_same_v<Wr, ibv_recv_wr>, rdma_verb &>;

public:
    rdma_verb() = default;

    template <typename... MemSlices>
    rdma_verb(MemSlices const &... sgl_entries) {
        sgl.clear();
        add_sgl_entry(sgl_entries...);
    }

    rdma_verb(rdma_verb const &other)
        : sgl(other.sgl), notified(other.notified) {}

    rdma_verb(rdma_verb &&other)
        : sgl(std::move(other.sgl)), notified(std::move(other.notified)) {}

    ~rdma_verb() = default;

    //! \brief Sets the opcode.
    template <ibv_exp_wr_opcode Opcode>
    self_if_send set_op(wr_type_base<Opcode> const &op) {
        opcode = Opcode;
        return *this;
    }

    //! \brief Sets the scatter-gather list.
    //!
    //! Each parameter accounts for a scatter-gather list entry.
    //! The original scatter-gather list will be cleared.
    template <typename... MemSlice>
    rdma_verb &set_sgl_entry(MemSlice... slices) {
        sgl.clear();
        length = 0;
        constructed_real_sgl = false;
        return add_sgl_entry(slices...);
    }

    //! \brief Appends entries to the scatter-gather list.
    template <typename... MemSlice>
    rdma_verb &add_sgl_entry(rdma_memory_slice const &head, MemSlice... tail) {
        // reject long sg-lists at compile time
        static_assert(1 + sizeof...(tail) <= kMaxSge,
                      "too many scatter-gather list entries");
        // then, we can only prevent long sg-lists at runtime
        RDMALIB2_ASSERT(sgl.size() < kMaxSge);

        sgl.emplace_back(std::ref(head));
        length += head.get_size();
        constructed_real_sgl = false;
        if constexpr (sizeof...(tail) == 0) {
            return *this;
        } else {
            return add_sgl_entry(tail...);
        }
    }

    size_t get_total_msg_length() const { return length; }

    std::pair<ibv_sge *, int> get_sgl() {
        if (!constructed_real_sgl) {
            construct_sgl();
        }
        return {real_sgl.data(), static_cast<int>(real_sgl.size())};
    }

    self_if_send set_remote_memory(rdma_remote_memory_slice const &remote) {
        if (unlikely(opcode == IBV_EXP_WR_SEND ||
                     opcode == IBV_EXP_WR_SEND_WITH_IMM)) {
            spdlog::warn("specifying remote memory for send verbs");
        } else if (unlikely(remote.get_size() < length)) {
            spdlog::warn(
                "remote memory slice size {} is smaller than SGL size {}",
                remote.get_size(), length);
        }
        this->remote = remote;
        return *this;
    }

    self_if_send set_notify(bool notify) {
        notified = notify;
        return *this;
    }

    self_if_send set_notified() { return set_notify(true); }

    self_if_send set_unnotified() { return set_notify(false); }

    self_if_send set_imm_data(uint32_t imm_data) {
        if (unlikely(opcode.has_value() && opcode != IBV_EXP_WR_SEND_WITH_IMM &&
                     opcode != IBV_EXP_WR_RDMA_WRITE_WITH_IMM)) {
            spdlog::warn(
                "setting immediate data for non-imm (send/write) verb");
        }
        this->imm_data = imm_data;
        return *this;
    }

    self_if_send set_cas(uint64_t compare, uint64_t swap) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn("setting CAS overwrites opcode for non-CAS verb");
        }
        opcode = IBV_EXP_WR_ATOMIC_CMP_AND_SWP;
        this->compare_add = compare;
        this->swap = swap;
        return *this;
    }

    self_if_send set_faa(uint64_t add) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn("setting FAA overwrites opcode for non-FAA verb");
        }
        opcode = IBV_EXP_WR_ATOMIC_FETCH_AND_ADD;
        this->compare_add = add;
        return *this;
    }

    self_if_send set_masked_cas(uint64_t compare, uint64_t swap,
                                uint64_t compare_mask, uint64_t swap_mask) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn(
                "setting masked-CAS overwrites opcode for non-masked-CAS verb");
        }
        opcode = IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP;
        this->compare_add = compare;
        this->swap = swap;
        this->compare_add_mask = compare_mask;
        this->swap_mask = swap_mask;
        return *this;
    }

    self_if_send set_masked_faa(uint64_t add, uint64_t add_mask) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn(
                "setting masked-FAA overwrites opcode for non-masked-FAA verb");
        }
        opcode = IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD;
        this->compare_add = add;
        this->compare_add_mask = add_mask;
        return *this;
    }

    self_if_send set_compare(uint64_t compare) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_CMP_AND_SWP &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn("setting CAS.compare for non-CAS verb");
        }
        this->compare_add = compare;
        return *this;
    }

    self_if_send set_swap(uint64_t swap) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_CMP_AND_SWP &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn("setting CAS.swap for non-CAS verb");
        }
        this->swap = swap;
        return *this;
    }

    self_if_send set_compare_mask(uint64_t compare_mask) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn(
                "setting masked-CAS.compare_mask for non-masked-CAS verb");
        }
        this->compare_add_mask = compare_mask;
        return *this;
    }

    self_if_send set_swap_mask(uint64_t swap_mask) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn(
                "setting masked-CAS.swap_mask for non-masked-CAS verb");
        }
        this->swap_mask = swap_mask;
        return *this;
    }

    self_if_send set_add(uint64_t add) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_FETCH_AND_ADD &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn("setting FAA.add for non-FAA verb");
        }
        this->compare_add = add;
        return *this;
    }

    self_if_send set_add_mask(uint64_t add_mask) {
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn("setting masked-FAA.add_mask for non-masked-FAA verb");
        }
        this->compare_add_mask = add_mask;
        return *this;
    }

    template <ibv_qp_type Type> void execute(rdma_qp<Type> const &qp);

public:
    typedef wr_type_base<IBV_EXP_WR_SEND> send;
    typedef wr_type_base<IBV_EXP_WR_SEND_WITH_IMM> send_imm;
    typedef wr_type_base<IBV_EXP_WR_RDMA_WRITE> write;
    typedef wr_type_base<IBV_EXP_WR_RDMA_WRITE_WITH_IMM> write_imm;
    typedef wr_type_base<IBV_EXP_WR_RDMA_READ> read;
    typedef wr_type_base<IBV_EXP_WR_ATOMIC_CMP_AND_SWP> cas;
    typedef wr_type_base<IBV_EXP_WR_ATOMIC_FETCH_AND_ADD> faa;
    typedef wr_type_base<IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP> masked_cas;
    typedef wr_type_base<IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD> masked_faa;

protected:
    void construct_sgl() {
        real_sgl.resize(sgl.size());
        for (size_t i = 0; i < sgl.size(); ++i) {
            real_sgl[i] = sgl[i].get().to_sge();
        }
        constructed_real_sgl = true;
    }

    std::optional<ibv_exp_wr_opcode> opcode = std::nullopt;
    std::vector<std::reference_wrapper<rdma_memory_slice const>> sgl = {};
    size_t length = 0;
    std::vector<ibv_sge> real_sgl = {};
    bool constructed_real_sgl = false;

    std::optional<rdma_remote_memory_slice> remote = std::nullopt;
    bool notified = false;
    bool carry_imm = false;
    uint32_t imm_data = 0;

    uint64_t compare_add = 0;
    uint64_t swap = 0;
    uint64_t compare_add_mask = 0;
    uint64_t swap_mask = 0;
};

typedef rdma_verb<ibv_exp_send_wr> rdma_send;
typedef rdma_verb<ibv_recv_wr> rdma_recv;

} // namespace rdmalib2

#endif // __RDMALIB2_PREDECLARE_VERB_H__
