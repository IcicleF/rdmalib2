#pragma once

#ifndef __RDMALIB2_CQ_H__
#define __RDMALIB2_CQ_H__

#include "context.h"
#include <new>
#include <optional>
#include <vector>

namespace rdmalib2 {

struct rdma_success_cqe {
    enum class op_type : uint32_t {
        Send,
        RdmaWrite,
        RdmaRead,
        AtomicCas,
        AtomicFaa,
        BindMw,
        LocalInv,

        Recv = 1 << 7,
        RecvWithImm,
    };

    op_type op;
    uint64_t wr_id;
    uint32_t length;
    uint32_t imm_data;

    static constexpr op_type to_op_type(ibv_wc_opcode op) {
        return static_cast<op_type>(op);
    }
};

class rdma_cq {
public:
    rdma_cq(rdma_context const &ctx, int cq_depth = kCqDepth,
            void *cq_context = nullptr) {
        auto cq = create_rdma_cq(ctx, cq_depth, cq_context);
        if (cq.has_value()) {
            this->cq = cq.value();
            spdlog::trace(
                "created completion queue {:p} with depth {} for context {:p}",
                reinterpret_cast<void *>(this->cq), cq_depth,
                reinterpret_cast<void const *>(ctx.get_context()));
        } else {
            spdlog::error("failed to create completion queue with depth {} for "
                          "context {:p}",
                          cq_depth,
                          reinterpret_cast<void const *>(ctx.get_context()));
            panic_with_errno();
        }
    }

    rdma_cq(rdma_cq const &) = delete;
    rdma_cq &operator=(rdma_cq const &) = delete;

    rdma_cq(rdma_cq &&other) noexcept : cq(other.cq) { other.cq = nullptr; }

    rdma_cq &operator=(rdma_cq &&other) & noexcept {
        if (this != &other) {
            this->~rdma_cq();
            new (this) rdma_cq(std::move(other));
        }
        return *this;
    }

    ~rdma_cq() {
        if (cq) {
            spdlog::trace("destroying completion queue {:p}",
                          reinterpret_cast<void *>(cq));
            ibv_destroy_cq(cq);
            cq = nullptr;
        }
    }

    ibv_cq *get_cq() const { return cq; }

    void poll(int num_entries = 1) const {
        ibv_wc wc[kMaxPollCq] = {};
        while (num_entries) {
            int entries_to_poll = std::min(num_entries, kMaxPollCq);
            int n = do_poll(entries_to_poll, wc);
            num_entries -= n;
        }
    }

    std::vector<rdma_success_cqe> poll_with_wc(int num_entries = 1) const {
        std::vector<rdma_success_cqe> ret{static_cast<size_t>(num_entries)};
        ibv_wc wc[kMaxPollCq] = {};
        int tot = 0;
        while (tot < num_entries) {
            int entries_to_poll = std::min(num_entries - tot, kMaxPollCq);
            int n = do_poll(entries_to_poll, wc);
            for (int i = 0; i < n; ++i) {
                ret[tot + i] = {
                    rdma_success_cqe::to_op_type(wc[i].opcode),
                    wc[i].wr_id,
                    wc[i].byte_len,
                    wc[i].imm_data,
                };
            }
            tot += n;
        }
        return ret;
    }

    int try_poll(int num_entries = 1) const {
        ibv_wc wc[kMaxPollCq] = {};
        int ret = 0;
        while (ret < num_entries) {
            int entries_to_poll = std::min(num_entries - ret, kMaxPollCq);
            int n = do_poll(num_entries, wc);
            ret += n;
            if (n < entries_to_poll) {
                break;
            }
        }
        return ret;
    }

    std::vector<rdma_success_cqe> try_poll_with_wc(int num_entries = 1) const {
        std::vector<rdma_success_cqe> ret{static_cast<size_t>(num_entries)};
        ibv_wc wc[kMaxPollCq] = {};
        int tot = 0;
        while (tot < num_entries) {
            int entries_to_poll = std::min(num_entries, kMaxPollCq);
            int n = do_poll(entries_to_poll, wc);
            for (int i = 0; i < n; ++i) {
                ret[tot + i] = {
                    rdma_success_cqe::to_op_type(wc[i].opcode),
                    wc[i].wr_id,
                    wc[i].byte_len,
                    wc[i].imm_data,
                };
            }
            tot += n;
            if (n < entries_to_poll) {
                break;
            }
        }
        ret.resize(tot);
        ret.shrink_to_fit();
        return ret;
    }

protected:
    static std::optional<ibv_cq *>
    create_rdma_cq(rdma_context const &ctx, int cq_depth, void *cq_context) {
        ibv_cq *cq = nullptr;
        auto rd = ctx.get_res_domain();
        if (rd.has_value()) {
            ibv_exp_cq_init_attr init_attr = {};
            init_attr.comp_mask = IBV_EXP_CQ_INIT_ATTR_RES_DOMAIN;
            init_attr.res_domain = rd.value();
            cq = ibv_exp_create_cq(ctx.get_context(), cq_depth, cq_context,
                                   nullptr, 0, &init_attr);
        } else {
            cq = ibv_create_cq(ctx.get_context(), cq_depth, cq_context, nullptr,
                               0);
        }
        return cq ? std::make_optional(cq) : std::nullopt;
    }

    int do_poll(int num_entries, ibv_wc *wc) const {
        int ret = ibv_poll_cq(cq, num_entries, wc);
        for (int i = 0; i < ret; ++i) {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS)) {
                spdlog::error("poll completion queue {:p} failed at <wr_id {}, "
                              "type {}> with status {}",
                              reinterpret_cast<void *>(cq), i, wc[i].wr_id,
                              wc[i].opcode, wc[i].status);
                panic_with_errno();
            }
        }
        return ret;
    }

    ibv_cq *cq = nullptr;
};

} // namespace rdmalib2

#endif
