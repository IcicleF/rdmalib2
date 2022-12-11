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

template <ibv_exp_wr_opcode Opcode> struct wr_type_base {
    static constexpr ibv_exp_wr_opcode opcode = Opcode;
};

static constexpr wr_type_base<IBV_EXP_WR_SEND> op_send = {};
static constexpr wr_type_base<IBV_EXP_WR_SEND_WITH_IMM> op_send_imm = {};
static constexpr wr_type_base<IBV_EXP_WR_RDMA_WRITE> op_write = {};
static constexpr wr_type_base<IBV_EXP_WR_RDMA_WRITE_WITH_IMM> op_write_imm = {};
static constexpr wr_type_base<IBV_EXP_WR_RDMA_READ> op_read = {};
static constexpr wr_type_base<IBV_EXP_WR_ATOMIC_CMP_AND_SWP> op_cas = {};
static constexpr wr_type_base<IBV_EXP_WR_ATOMIC_FETCH_AND_ADD> op_faa = {};
static constexpr wr_type_base<IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP>
    op_masked_cas = {};
static constexpr wr_type_base<IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD>
    op_masked_faa = {};

template <typename Wr> class rdma_verb {
public:
    // Expose work request type for public use
    using wr_type = Wr;

public:
    rdma_verb() = default;

    template <typename... MemSlices> rdma_verb(MemSlices... sgl_entries) {
        set_sgl_entry(std::forward<MemSlices...>(sgl_entries...));
    }

    rdma_verb(rdma_verb const &other)
        : sgl(other.sgl), notified(other.notified) {}

    rdma_verb(rdma_verb &&other) noexcept
        : sgl(std::move(other.sgl)), notified(std::move(other.notified)) {}

    rdma_verb &operator=(rdma_verb const &other) & {
        sgl = other.sgl;
        notified = other.notified;
        return *this;
    }

    rdma_verb &operator=(rdma_verb &&other) & noexcept {
        sgl = std::move(other.sgl);
        notified = std::move(other.notified);
        return *this;
    }

    ~rdma_verb() = default;

    //! \brief Constructs, caches, and gets the corresponding work request
    //! structure of the current verb object.
    Wr const &get_wr() {
        if constexpr (std::is_same_v<Wr, ibv_exp_send_wr>) {
            if (unlikely(!opcode.has_value())) {
                spdlog::error("constructing send work request with no opcode");
                panic();
            }
        }
        construct_wr();
        wr.next = nullptr;
        return wr;
    }

    //! \brief Temporarily sets the next work request in the chain.
    //! The next work request pointer will be reset after the next call to
    //! get_wr().
    rdma_verb<Wr> &set_next(rdma_verb const &next) {
        wr.next = const_cast<Wr *>(&next.get_wr());
        return *this;
    }

    //! \brief Clears the next work request pointer.
    rdma_verb<Wr> &clear_next() {
        wr.next = nullptr;
        return *this;
    }

    //! \brief Sets the work request ID.
    rdma_verb<Wr> &set_id(uint64_t id) {
        if (wr_id != id) {
            wr_id = id;
            constructed_wr = false;
        }
        return *this;
    }

    //! \brief Sets the opcode.
    template <ibv_exp_wr_opcode Opcode>
    rdma_verb<Wr> &set_op(wr_type_base<Opcode> const &op) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set opcode for recv verb");
        if (opcode != Opcode) {
            opcode = Opcode;
            constructed_wr = false;
        }
        return *this;
    }

    //! \brief Gets the opcode.
    std::optional<ibv_exp_wr_opcode> get_op() const {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot acquire opcode from recv verb");
        return opcode;
    }

    //! \brief Sets the scatter-gather list.
    //!
    //! Each parameter accounts for a scatter-gather list entry.
    //! The original scatter-gather list will be cleared.
    template <typename... MemSlice>
    rdma_verb<Wr> &set_sgl_entry(MemSlice... slices) {
        sgl.clear();
        length = 0;
        constructed_real_sgl = false;
        return add_sgl_entry(std::forward<MemSlice...>(slices...));
    }

    //! \brief Appends entries to the scatter-gather list.
    template <typename... MemSlice>
    rdma_verb<Wr> &add_sgl_entry(rdma_memory_slice const &head,
                                 MemSlice... tail) {
        // reject long sg-lists at compile time
        static_assert(1 + sizeof...(tail) <= kMaxSge,
                      "too many scatter-gather list entries");
        // then, we can only prevent long sg-lists at runtime
        RDMALIB2_ASSERT(sgl.size() < kMaxSge);

        sgl.emplace_back(head);
        length += head.get_size();
        constructed_real_sgl = false;
        if constexpr (sizeof...(tail) == 0) {
            return *this;
        } else {
            return add_sgl_entry(std::forward<MemSlice...>(tail...));
        }
    }

    size_t get_total_msg_length() const { return length; }

    rdma_verb<Wr> &set_remote_memory(rdma_remote_memory_slice const &remote) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set remote memory for recv verb");
        if (unlikely(opcode == IBV_EXP_WR_SEND ||
                     opcode == IBV_EXP_WR_SEND_WITH_IMM)) {
            spdlog::warn("specifying remote memory for send verbs");
        } else if (unlikely(remote.get_size() < length)) {
            spdlog::warn(
                "remote memory slice size {} is smaller than SGL size {}",
                remote.get_size(), length);
        }
        this->remote = remote;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_notify(bool notify) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set notify for recv verb");
        if (notified != notify) {
            notified = notify;
            constructed_wr = false;
        }
        return *this;
    }

    rdma_verb<Wr> &set_notified() { return set_notify(true); }

    rdma_verb<Wr> &set_unnotified() { return set_notify(false); }

    bool is_notified() const {
        if constexpr (std::is_same_v<Wr, ibv_exp_send_wr>) {
            return notified;
        }
        return true;
    }

    rdma_verb<Wr> &set_imm(uint32_t imm_data) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set immediate data for recv verb");
        if (unlikely(opcode.has_value() && opcode != IBV_EXP_WR_SEND_WITH_IMM &&
                     opcode != IBV_EXP_WR_RDMA_WRITE_WITH_IMM)) {
            spdlog::warn(
                "setting immediate data for non-imm (send/write) verb");
        }
        this->carry_imm = true;
        this->imm_data = imm_data;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &clear_imm() {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot clear immediate data for recv verb");
        if (this->carry_imm) {
            this->carry_imm = false;
            constructed_wr = false;
        }
        return *this;
    }

    rdma_verb<Wr> &set_cas(uint64_t compare, uint64_t swap) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set CAS for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn("setting CAS overwrites opcode for non-CAS verb");
        }
        opcode = IBV_EXP_WR_ATOMIC_CMP_AND_SWP;
        this->compare_add = compare;
        this->swap = swap;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_faa(uint64_t add) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set FAA for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn("setting FAA overwrites opcode for non-FAA verb");
        }
        opcode = IBV_EXP_WR_ATOMIC_FETCH_AND_ADD;
        this->compare_add = add;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_masked_cas(uint64_t compare, uint64_t swap,
                                  uint64_t compare_mask, uint64_t swap_mask) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set masked-CAS for recv verb");
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
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_masked_faa(uint64_t add, uint64_t add_mask) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set masked-FAA for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn(
                "setting masked-FAA overwrites opcode for non-masked-FAA verb");
        }
        opcode = IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD;
        this->compare_add = add;
        this->compare_add_mask = add_mask;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_compare(uint64_t compare) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set CAS.compare for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_CMP_AND_SWP &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn("setting CAS.compare for non-CAS verb");
        }
        this->compare_add = compare;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_swap(uint64_t swap) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set CAS.swap for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_CMP_AND_SWP &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn("setting CAS.swap for non-CAS verb");
        }
        this->swap = swap;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_compare_mask(uint64_t compare_mask) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set masked-CAS.compare_mask for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn(
                "setting masked-CAS.compare_mask for non-masked-CAS verb");
        }
        this->compare_add_mask = compare_mask;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_swap_mask(uint64_t swap_mask) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set masked-CAS.swap_mask for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)) {
            spdlog::warn(
                "setting masked-CAS.swap_mask for non-masked-CAS verb");
        }
        this->swap_mask = swap_mask;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_add(uint64_t add) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set FAA.add for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_ATOMIC_FETCH_AND_ADD &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn("setting FAA.add for non-FAA verb");
        }
        this->compare_add = add;
        constructed_wr = false;
        return *this;
    }

    rdma_verb<Wr> &set_add_mask(uint64_t add_mask) {
        static_assert(std::is_same_v<Wr, ibv_exp_send_wr>,
                      "cannot set masked-FAA.add_mask for recv verb");
        if (unlikely(opcode.has_value() &&
                     opcode != IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)) {
            spdlog::warn("setting masked-FAA.add_mask for non-masked-FAA verb");
        }
        this->compare_add_mask = add_mask;
        constructed_wr = false;
        return *this;
    }

    template <ibv_qp_type Type> void execute(rdma_qp<Type> const &qp);

protected:
    void construct_sgl() {
        if (!constructed_real_sgl) {
            real_sgl.resize(sgl.size());
            for (size_t i = 0; i < sgl.size(); ++i) {
                real_sgl[i] = sgl[i].to_sge();
            }
            constructed_real_sgl = true;
        }
    }

    void construct_wr() {
        construct_sgl();
        if (!constructed_wr) {
            wr = Wr{};
            wr.wr_id = wr_id;
            wr.next = nullptr;
            wr.sg_list = real_sgl.data();
            wr.num_sge = real_sgl.size();

            if constexpr (std::is_same_v<Wr, ibv_exp_send_wr>) {
                wr.exp_opcode = opcode.value();

                // CQE generation
                if (notified) {
                    wr.exp_send_flags |= IBV_EXP_SEND_SIGNALED;
                }

                // Immediate data
                if (carry_imm) {
                    wr.ex.imm_data = imm_data;
                }

                // Non-send verbs require specifying remote memory
                if (opcode != IBV_EXP_WR_SEND &&
                    opcode != IBV_EXP_WR_SEND_WITH_IMM) {
                    RDMALIB2_ASSERT(remote.has_value());
                }

                if (opcode == IBV_EXP_WR_RDMA_READ ||
                    opcode == IBV_EXP_WR_RDMA_WRITE ||
                    opcode == IBV_EXP_WR_RDMA_WRITE_WITH_IMM) {
                    // read/write
                    wr.wr.rdma.remote_addr = remote->get_addr();
                    wr.wr.rdma.rkey = remote->get_rkey();
                } else if (opcode == IBV_EXP_WR_ATOMIC_CMP_AND_SWP ||
                           opcode == IBV_EXP_WR_ATOMIC_FETCH_AND_ADD) {
                    // atomics
                    RDMALIB2_ASSERT(is_atomic_capable());

                    wr.wr.atomic.remote_addr = remote->get_addr();
                    wr.wr.atomic.rkey = remote->get_rkey();
                    wr.wr.atomic.compare_add = compare_add;
                    wr.wr.atomic.swap = swap;
                } else if (opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP ||
                           opcode ==
                               IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD) {
                    // masked atomics
                    RDMALIB2_ASSERT(is_atomic_capable());

                    wr.ext_op.masked_atomics.remote_addr = remote->get_addr();
                    wr.ext_op.masked_atomics.rkey = remote->get_rkey();

                    if (opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP) {
                        wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap
                            .compare_val = compare_add;
                        wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap
                            .swap_val = swap;
                        wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap
                            .compare_mask = compare_add_mask;
                        wr.ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap
                            .swap_mask = swap_mask;
                    } else {
                        wr.ext_op.masked_atomics.wr_data.inline_data.op
                            .fetch_add.add_val = compare_add;
                        wr.ext_op.masked_atomics.wr_data.inline_data.op
                            .fetch_add.field_boundary = compare_add_mask;
                    }
                } else if (opcode != IBV_EXP_WR_SEND &&
                           opcode != IBV_EXP_WR_SEND_WITH_IMM) {
                    spdlog::error("unsupported work request type: {}", *opcode);
                    panic();
                }
            }

            constructed_wr = true;
        }
    }

    bool is_atomic_capable() const {
        return length == sizeof(uint64_t) && sgl.size() == 1 &&
               sgl[0].is_aligned();
    }

    // Cached sglist & work request
    std::vector<ibv_sge> real_sgl = {};
    bool constructed_real_sgl = false;
    Wr wr;
    bool constructed_wr = false;

    // Original information
    uint64_t wr_id;
    std::optional<ibv_exp_wr_opcode> opcode = std::nullopt;
    std::vector<rdma_memory_slice> sgl = {};
    size_t length = 0;
    std::optional<rdma_remote_memory_slice> remote = std::nullopt;
    bool notified = false;
    bool carry_imm = false;
    uint32_t imm_data = 0;
    uint64_t compare_add = 0;
    uint64_t swap = 0;
    uint64_t compare_add_mask = 0;
    uint64_t swap_mask = 0;
};

typedef rdma_verb<ibv_exp_send_wr> rdma_send_family;
typedef rdma_verb<ibv_recv_wr> rdma_recv;

} // namespace rdmalib2

#endif // __RDMALIB2_PREDECLARE_VERB_H__
