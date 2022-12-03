#pragma once

#ifndef __RDMALIB2_PREDECLARE_QP_H__
#define __RDMALIB2_PREDECLARE_QP_H__

#include "../context.h"
#include "../cq.h"
#include <new>
#include <optional>
#include <string>

namespace rdmalib2 {

static inline std::string qptype_to_string(ibv_qp_type type) {
    switch (type) {
    case IBV_QPT_RC:
        return "RC";
    case IBV_QPT_UC:
        return "UC";
    case IBV_QPT_UD:
        return "UD";
    case IBV_QPT_RAW_PACKET:
        return "RAW_PACKET";
    case IBV_QPT_XRC_SEND:
        return "XRC_SEND";
    case IBV_QPT_XRC_RECV:
        return "XRC_RECV";
    case IBV_EXP_QPT_DC_INI:
        return "DC_INI";
    default:
        spdlog::error("unknown queue pair type {}", type);
        panic();
    }
}

// Predeclaration of rdma_verb class
template <typename Wr> class rdma_verb;

template <ibv_qp_type Type> class rdma_qp {
protected:
    template <uint32_t CompMask, uint32_t CreateFlags> struct qp_feature_base {
        static constexpr uint32_t comp_mask = CompMask;
        static constexpr uint32_t create_flags = CreateFlags;

        template <uint32_t CompMask2, uint32_t CreateFlags2>
        constexpr qp_feature_base<CompMask | CompMask2,
                                  CreateFlags | CreateFlags2>
        operator+(qp_feature_base<CompMask2, CreateFlags2>) const {
            return {};
        }
    };

public:
    struct info {
        ibv_gid gid;
        uint32_t lid;
        uint32_t qp_num;
        uint32_t psn;

        info(ibv_gid gid, uint32_t lid, uint32_t qp_num, uint32_t psn)
            : gid(gid), lid(lid), qp_num(qp_num), psn(psn) {}
    };

    info get_info() const {
        return {ctx.get_gid(), ctx.get_port_lid(port), qp->qp_num,
                universal_init_psn};
    }

public:
    template <uint32_t C, uint32_t F>
    rdma_qp(rdma_context const &ctx, int qp_depth, rdma_cq const &send_cq,
            rdma_cq const &recv_cq, qp_feature_base<C, F> const &features)
        : ctx(ctx) {
        static_assert(Type != IBV_QPT_XRC_SEND, "XRC not implemented");
        static_assert(Type != IBV_QPT_XRC_RECV, "XRC not implemented");
        static_assert(Type != IBV_EXP_QPT_DC_INI, "DC QP not implemented");

        auto qp =
            create_rdma_qp(ctx, Type, qp_depth, send_cq, recv_cq, features);
        if (qp.has_value()) {
            this->qp = qp.value();
            spdlog::trace(
                "created queue pair {:p}, type {}, depth {} for context {:p}",
                reinterpret_cast<void *>(this->qp), qptype_to_string(Type),
                qp_depth, reinterpret_cast<void const *>(&ctx));
        } else {
            spdlog::error(
                "failed to create queue pair with type {}, depth {} for "
                "context {:p}",
                qptype_to_string(Type), qp_depth,
                reinterpret_cast<void const *>(&ctx));
            panic_with_errno();
        }
    }

    rdma_qp(rdma_qp const &) = delete;
    rdma_qp &operator=(rdma_qp const &) = delete;

    rdma_qp(rdma_qp &&other) noexcept : ctx(other.ctx), qp(other.qp) {
        other.qp = nullptr;
    }

    rdma_qp &operator=(rdma_qp &&other) noexcept {
        if (this != &other) {
            this->~rdma_qp();
            new (this) rdma_qp(std::move(other));
        }
        return *this;
    }

    ~rdma_qp() {
        if (qp) {
            spdlog::trace("destroying queue pair {:p}",
                          reinterpret_cast<void *>(qp));
            ibv_destroy_qp(qp);
            qp = nullptr;
        }
    }

    ibv_qp *get_qp() const { return qp; }

    rdma_qp &bind_port(uint8_t port = 1) {
        this->port = port;
        if constexpr (Type == IBV_QPT_UD || Type == IBV_QPT_RAW_PACKET) {
            static constexpr uint32_t ud_qkey = 0x11111111;

            modify_qp_to_init(qp, port, ud_qkey);
            modify_qp_to_rtr(qp, {}, 0, 0, universal_init_psn, port);
            modify_qp_to_rts(qp, universal_init_psn);
        }
        return *this;
    }

    rdma_qp &connect(info const &remote, uint8_t port = 1) {
        RDMALIB2_ASSERT(qp->qp_type == IBV_QPT_RC);

        modify_qp_to_init(qp, port);
        modify_qp_to_rtr(qp, remote.gid, remote.lid, remote.qp_num, remote.psn,
                         port);
        modify_qp_to_rts(qp, universal_init_psn);
        return *this;
    }

    info get_qp_info() const {
        return {ctx.get_gid(port), ctx.get_port_lid(port), qp->qp_num,
                universal_init_psn};
    }

    template <typename Wr> void post_verb(rdma_verb<Wr> &) const;

    template <typename ForwardIt>
    void post_verb(ForwardIt first, ForwardIt last) const;

public:
    typedef qp_feature_base<0, 0> no_features;
    typedef qp_feature_base<IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG, 0>
        extended_atomics;
    typedef qp_feature_base<IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS,
                            IBV_EXP_QP_CREATE_EC_PARITY_EN>
        erasure_coding;

protected:
    template <uint32_t CompMask, uint32_t CreateFlags>
    static std::optional<ibv_qp *>
    create_rdma_qp(rdma_context const &ctx, int qp_depth,
                   rdma_cq const &send_cq, rdma_cq const &recv_cq,
                   qp_feature_base<CompMask, CreateFlags> const &features) {
        ibv_exp_qp_init_attr init_attr = {};
        init_attr.send_cq = send_cq.get_cq();
        init_attr.recv_cq = recv_cq.get_cq();
        init_attr.cap.max_send_wr = qp_depth;
        init_attr.cap.max_recv_wr = qp_depth;
        init_attr.cap.max_send_sge = kMaxSge;
        init_attr.cap.max_recv_sge = kMaxSge;
        init_attr.cap.max_inline_data = kMaxInlineData;
        init_attr.qp_type = Type;
        init_attr.comp_mask = IBV_EXP_QP_INIT_ATTR_PD;
        init_attr.pd = ctx.get_pd();

        auto rd = ctx.get_res_domain();
        if (rd.has_value()) {
            init_attr.comp_mask |= IBV_EXP_QP_INIT_ATTR_RES_DOMAIN;
            init_attr.res_domain = rd.value();
        }

        // Extended atomics feature
        if constexpr (CompMask & extended_atomics::comp_mask) {
            static_assert(Type == IBV_QPT_RC,
                          "extended atomics only supported for RC QPs");

            init_attr.comp_mask |= extended_atomics::comp_mask;
            init_attr.exp_create_flags |= extended_atomics::create_flags;
            init_attr.max_atomic_arg = sizeof(uint64_t);
        }

        // Erasure coding offloading feature
        if constexpr (CompMask & erasure_coding::comp_mask) {
            init_attr.comp_mask |= erasure_coding::comp_mask;
            init_attr.exp_create_flags |= erasure_coding::create_flags;
        }

        ibv_qp *qp = ibv_exp_create_qp(ctx.get_context(), &init_attr);
        return qp ? std::make_optional(qp) : std::nullopt;
    }

    static void modify_qp_to_init(ibv_qp *qp, uint8_t port_num = 1,
                                  uint32_t ud_qkey = 0) {
        RDMALIB2_ASSERT(qp->state == IBV_QPS_INIT);

        ibv_qp_attr attr = {};
        attr.qp_state = IBV_QPS_INIT;
        attr.port_num = port_num;
        attr.pkey_index = 0;

        int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT;

        if constexpr (Type == IBV_QPT_RC) {
            attr.qp_access_flags = IBV_ACCESS_REMOTE_READ |
                                   IBV_ACCESS_REMOTE_WRITE |
                                   IBV_ACCESS_REMOTE_ATOMIC;
            flags |= IBV_QP_ACCESS_FLAGS;
        } else if constexpr (Type == IBV_QPT_UC) {
            attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
            flags |= IBV_QP_ACCESS_FLAGS;
        } else if constexpr (Type == IBV_QPT_UD) {
            attr.qkey = ud_qkey;
            flags |= IBV_QP_QKEY;
        } else if constexpr (Type == IBV_QPT_RAW_PACKET) {
            flags &= ~IBV_QP_PKEY_INDEX;
        } else {
            spdlog::error("currently unsupported QP type {}", qp->qp_type);
        }

        int ret = ibv_modify_qp(qp, &attr, flags);
        if (ret) {
            spdlog::error("failed to modify QP {:p} to INIT state",
                          reinterpret_cast<void *>(qp));
            panic_with_errno();
        }
    } // namespace rdmalib2

    static void modify_qp_to_rtr(ibv_qp *qp, ibv_gid remote_gid,
                                 uint32_t remote_lid, uint32_t remote_qpn,
                                 uint32_t psn, uint32_t port = 1) {
        RDMALIB2_ASSERT(qp->state == IBV_QPS_INIT);

        ibv_qp_attr attr = {};
        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = IBV_MTU_4096;
        attr.dest_qp_num = remote_qpn;
        attr.rq_psn = psn;

        auto &ah = attr.ah_attr;
        ah.dlid = remote_lid;
        ah.sl = 0;
        ah.src_path_bits = 0;
        ah.port_num = port;

        ah.is_global = 1;
        ah.grh.dgid = remote_gid;
        ah.grh.hop_limit = 0xFF;
        ah.grh.sgid_index = rdma_context::universal_gid_index;
        ah.grh.traffic_class = 0;

        int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                    IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;

        if constexpr (Type == IBV_QPT_RC) {
            attr.max_dest_rd_atomic = 16;
            attr.min_rnr_timer = 12;
            flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
        } else if constexpr (Type == IBV_QPT_UD || Type == IBV_QPT_RAW_PACKET) {
            flags = IBV_QP_STATE;
        }

        int ret = ibv_modify_qp(qp, &attr, flags);
        if (ret) {
            spdlog::error("failed to modify QP {:p} to RTR state",
                          reinterpret_cast<void *>(qp));
            panic_with_errno();
        }
    }

    static void modify_qp_to_rts(ibv_qp *qp, uint32_t psn) {
        RDMALIB2_ASSERT(qp->state == IBV_QPS_RTR);

        ibv_qp_attr attr = {};
        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = psn;

        int flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

        if constexpr (Type == IBV_QPT_RC) {
            attr.timeout = 14;
            attr.retry_cnt = 7;
            attr.rnr_retry = 6;
            attr.max_rd_atomic = 16;
            flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                     IBV_QP_MAX_QP_RD_ATOMIC;
        } else if constexpr (Type == IBV_QPT_RAW_PACKET) {
            flags = IBV_QP_STATE;
        }

        int ret = ibv_modify_qp(qp, &attr, flags);
        if (ret) {
            spdlog::error("failed to modify QP {:p} to RTS state",
                          reinterpret_cast<void *>(qp));
            panic_with_errno();
        }
    }

    rdma_context const &ctx;
    ibv_qp *qp = nullptr;
    uint8_t port = 1;

    static constexpr uint32_t universal_init_psn = 0;
}; // namespace rdmalib2

typedef rdma_qp<IBV_QPT_RAW_PACKET> rdma_raw_packet_qp;
typedef rdma_qp<IBV_QPT_RC> rdma_rc_qp;
typedef rdma_qp<IBV_QPT_UD> rdma_ud_qp;
typedef rdma_qp<IBV_QPT_XRC_SEND> rdma_xrc_send_qp;
typedef rdma_qp<IBV_QPT_XRC_RECV> rdma_xrc_recv_qp;
typedef rdma_qp<IBV_EXP_QPT_DC_INI> rdma_dc_qp;

} // namespace rdmalib2

#endif
