#pragma once

#ifndef __RDMALIB2_CONTEXT_H__
#define __RDMALIB2_CONTEXT_H__

#include <infiniband/verbs.h>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#include "common.h"
#include <spdlog/spdlog.h>

namespace rdmalib2 {

class rdma_context {
protected:
    template <uint32_t CompMask, uint32_t ThreadHint, uint32_t MsgHint>
    struct res_domain_hint_base {
        template <uint32_t CompMask2, uint32_t ThreadHint2, uint32_t MsgHint2>
        constexpr res_domain_hint_base<
            CompMask | CompMask2, ThreadHint | ThreadHint2, MsgHint | MsgHint2>
        operator+(
            res_domain_hint_base<CompMask2, ThreadHint2, MsgHint2>) const {
            return {};
        }
    };

public:
    rdma_context() : rdma_context("") {}

    rdma_context(std::string_view dev_name) {
        auto target = create_rdma_context(dev_name);
        if (target.has_value()) {
            ctx = std::get<0>(target.value());
            pd = std::get<1>(target.value());
            spdlog::trace(
                "created context {:p} and protection domain {:p} for device {}",
                reinterpret_cast<void *>(ctx), reinterpret_cast<void *>(pd),
                dev_name == "" ? ibv_get_device_name(ctx->device) : dev_name);

            if (ibv_exp_query_device(ctx, &dev_attr)) {
                spdlog::error("failed to query device attributes");
                panic_with_errno();
            }
            spdlog::trace("device {} has {} physical port(s)",
                          ibv_get_device_name(ctx->device),
                          dev_attr.phys_port_cnt);

            for (uint8_t i = 1; i <= dev_attr.phys_port_cnt; i++) {
                ibv_exp_port_attr port_attr = {};
                if (ibv_exp_query_port(ctx, i, &port_attr)) {
                    spdlog::error("failed to query port {}'s attributes", i);
                    panic_with_errno();
                }

                int gid_index = universal_gid_index % port_attr.gid_tbl_len;
                ibv_gid gid = {};
                if (ibv_query_gid(ctx, i, gid_index, &gid)) {
                    spdlog::error("failed to query port {}'s gid", i);
                    panic_with_errno();
                }
                spdlog::trace("port {}'s gid is {:x}-{:x}", i,
                              gid.global.subnet_prefix,
                              gid.global.interface_id);

                port_attrs.emplace_back(gid, port_attr);
            }
        } else {
            if (dev_name != "") {
                spdlog::error("failed to create context and/or protection "
                              "domain for device {}",
                              dev_name);
            } else {
                spdlog::error("failed to create context and/or protection "
                              "domain for default device");
            }
            panic_with_errno();
        }
    }

    template <uint32_t C, uint32_t T, uint32_t M>
    rdma_context(std::string_view dev_name,
                 res_domain_hint_base<C, T, M> const &hint)
        : rdma_context(dev_name) {
        auto target = create_rdma_res_domain(ctx, hint);
        if (target.has_value()) {
            rd = target.value();
            if (rd) {
                spdlog::trace("created resource domain {:p} for context {:p}",
                              reinterpret_cast<void *>(rd),
                              reinterpret_cast<void *>(ctx));
            } else {
                spdlog::trace(
                    "skipped resource domain creation for context {:p}",
                    reinterpret_cast<void *>(ctx));
            }
        } else {
            spdlog::error("failed to create resource domain for context "
                          "{:p}",
                          reinterpret_cast<void *>(ctx));
            panic_with_errno();
        }
    }

    rdma_context(const rdma_context &) = delete;
    rdma_context &operator=(const rdma_context &) = delete;

    rdma_context(rdma_context &&other) = delete;
    rdma_context &operator=(rdma_context &&other) = delete;

    ~rdma_context() {
        if (rd) {
            spdlog::trace("destroying resource domain {:p}",
                          reinterpret_cast<void *>(rd));
            ibv_exp_destroy_res_domain_attr destroy_attr = {};
            ibv_exp_destroy_res_domain(ctx, rd, &destroy_attr);
            rd = nullptr;
        }
        if (pd) {
            spdlog::trace("destroying protection domain {:p}",
                          reinterpret_cast<void *>(pd));
            ibv_dealloc_pd(pd);
            pd = nullptr;
        }
        if (ctx) {
            spdlog::trace("destroying context {:p}",
                          reinterpret_cast<void *>(ctx));
            ibv_close_device(ctx);
            ctx = nullptr;
        }
    }

    ibv_context *get_context() const { return ctx; }

    ibv_pd *get_pd() const { return pd; }

    std::optional<ibv_exp_res_domain *> get_res_domain() const {
        return rd ? std::make_optional(rd)
                  : std::optional<ibv_exp_res_domain *>{};
    }

    ibv_gid get_gid(uint8_t port = 1) const {
        if (port > port_attrs.size()) {
            spdlog::error("port {} is out of port count bound {}", port,
                          dev_attr.phys_port_cnt);
            panic();
        }
        return std::get<0>(port_attrs[port - 1]);
    }

    uint32_t get_port_lid(uint8_t port = 1) const {
        if (port > port_attrs.size()) {
            spdlog::error("port {} is out of port count bound {}", port,
                          dev_attr.phys_port_cnt);
            panic();
        }
        return std::get<1>(port_attrs[port - 1]).lid;
    }

public:
    static constexpr res_domain_hint_base<0, 0, 0> no_hints = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_THREAD_MODEL,
                                          IBV_EXP_THREAD_SAFE, 0>
        thread_safe = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_THREAD_MODEL,
                                          IBV_EXP_THREAD_UNSAFE, 0>
        thread_unsafe = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_THREAD_MODEL,
                                          IBV_EXP_THREAD_SINGLE, 0>
        thread_single = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_MSG_MODEL, 0,
                                          IBV_EXP_MSG_DEFAULT>
        msg_default = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_MSG_MODEL, 0,
                                          IBV_EXP_MSG_HIGH_BW>
        msg_high_bw = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_MSG_MODEL, 0,
                                          IBV_EXP_MSG_LOW_LATENCY>
        msg_low_latency = {};
    static constexpr res_domain_hint_base<IBV_EXP_RES_DOMAIN_MSG_MODEL, 0,
                                          IBV_EXP_MSG_FORCE_LOW_LATENCY>
        msg_force_low_latency = {};

protected:
    static std::optional<std::tuple<ibv_context *, ibv_pd *>>
    create_rdma_context(std::string_view name) {
        int n = 0;
        ibv_device **dev_list = ibv_get_device_list(&n);
        if (!dev_list || n == 0) {
            return {};
        }

        ibv_context *ctx = nullptr;
        for (int i = 0; i < n; ++i) {
            if (name.length() == 0 ||
                name == ibv_get_device_name(dev_list[i])) {
                ctx = ibv_open_device(dev_list[i]);
                break;
            }
        }
        ibv_free_device_list(dev_list);
        if (!ctx) {
            return {};
        }

        ibv_pd *pd = ibv_alloc_pd(ctx);
        if (!pd) {
            ibv_close_device(ctx);
            return {};
        }
        return std::make_optional(std::make_tuple(ctx, pd));
    }

    template <uint32_t CompMask, uint32_t ThreadHint, uint32_t MsgHint>
    static std::optional<ibv_exp_res_domain *> create_rdma_res_domain(
        ibv_context *ctx,
        res_domain_hint_base<CompMask, ThreadHint, MsgHint> const &) {
        if constexpr (CompMask == 0) {
            // When no hints given, return a null pointer as if the resource
            // domain has never been created.
            return std::make_optional(nullptr);
        } else {
            if (!ctx) {
                return {};
            }

            ibv_exp_res_domain_init_attr init_attr = {};
            init_attr.comp_mask = CompMask;
            init_attr.thread_model =
                static_cast<ibv_exp_thread_model>(ThreadHint);
            init_attr.msg_model = static_cast<ibv_exp_msg_model>(MsgHint);

            auto rd = ibv_exp_create_res_domain(ctx, &init_attr);
            return rd ? std::make_optional(rd) : std::nullopt;
        }
    }

    ibv_context *ctx = nullptr;
    ibv_pd *pd = nullptr;
    ibv_exp_res_domain *rd = nullptr;

    ibv_exp_device_attr dev_attr = {};
    std::vector<std::tuple<ibv_gid, ibv_exp_port_attr>> port_attrs;

public:
    // Use universal GID index 3 to fit both InfiniBand and RoCEv2
    static constexpr int universal_gid_index = 0;
};

} // namespace rdmalib2

#endif // __RDMALIB2_CONTEXT_H__
