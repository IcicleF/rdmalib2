#pragma once

#ifndef __RDMALIB2_CQ_H__
#define __RDMALIB2_CQ_H__

#include "context.h"
#include <new>
#include <optional>

namespace rdmalib2 {

class rdma_cq {
public:
    rdma_cq(rdma_context const &ctx, int cq_depth = default_cq_depth,
            void *cq_context = nullptr) {
        auto cq = create_rdma_cq(ctx, cq_depth, cq_context);
        if (cq.has_value()) {
            this->cq = cq.value();
            spdlog::trace(
                "created completion queue {:p} with depth {} for context {:p}",
                reinterpret_cast<void *>(this->cq), cq_depth,
                reinterpret_cast<void const *>(&ctx));
        } else {
            spdlog::error("failed to create completion queue with depth {} for "
                          "context {:p}",
                          cq_depth, reinterpret_cast<void const *>(&ctx));
            panic_with_errno();
        }
    }

    rdma_cq(rdma_cq const &) = delete;
    rdma_cq &operator=(rdma_cq const &) = delete;

    rdma_cq(rdma_cq &&other) noexcept {
        this->cq = other.cq;
        other.cq = nullptr;
    }

    rdma_cq &operator=(rdma_cq &&other) noexcept {
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

public:
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

    static constexpr int default_cq_depth = 256;

    ibv_cq *cq = nullptr;
};

} // namespace rdmalib2

#endif
