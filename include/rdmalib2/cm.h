#pragma once

#ifndef __RDMALIB2_CM_H__
#define __RDMALIB2_CM_H__

#include <functional>
#include <hrpc/client.h>
#include <hrpc/server.h>

#include "context.h"
#include "cq.h"
#include "mem.h"
#include "qp.h"

namespace rdmalib2 {

class cm {
public:
    using qp_callback_t =
        std::function<void(rdma_rc_qp qp, rdma_cq send_cq, rdma_cq recv_cq)>;
    using qp_callback_with_stop_t =
        std::function<bool(rdma_rc_qp qp, rdma_cq send_cq, rdma_cq recv_cq)>;

public:
    cm(rdma_context const &ctx) : ctx(ctx) {}

    cm(cm const &) = delete;
    cm &operator=(cm const &) = delete;

    cm(cm &&) = delete;
    cm &operator=(cm &&) = delete;

    void connect(rdma_rc_qp &qp, std::string_view ip,
                 uint16_t port = kRpcPort) {
        hrpc::client cli{ip, port};
        auto self_info = qp.get_info();
        auto info = cli.call<rdma_rc_qp::info>(RPC_ESTABLISH, self_info);

        spdlog::trace(
            "connected local qp <gid {:x}-{:x}, lid {}, qpn {}, psn {}> to "
            "remote qp <gid {:x}-{:x}, lid {}, qpn {}, psn {}>",
            self_info.gid.global.subnet_prefix,
            self_info.gid.global.interface_id, self_info.lid, self_info.qp_num,
            self_info.psn, info.gid.global.subnet_prefix,
            info.gid.global.interface_id, info.lid, info.qp_num, info.psn);
        qp.connect(info);
    }

    void run_server(qp_callback_t qp_callback, uint16_t port = kRpcPort) {
        hrpc::server svr{port};
        svr.bind(RPC_ESTABLISH, [this, qp_callback](rdma_rc_qp::info info) {
            rdma_cq send_cq{ctx}, recv_cq{ctx};
            rdma_rc_qp qp{ctx, send_cq, recv_cq, kQpDepth,
                          rdma_rc_qp::extended_atomics};
            qp.connect(info);

            auto self_info = qp.get_info();
            spdlog::trace(
                "connected local qp <gid {:x}-{:x}, lid {}, qpn {}, psn {}> to "
                "remote qp <gid {:x}-{:x}, lid {}, qpn {}, psn {}>",
                self_info.gid.global.subnet_prefix,
                self_info.gid.global.interface_id, self_info.lid,
                self_info.qp_num, self_info.psn, info.gid.global.subnet_prefix,
                info.gid.global.interface_id, info.lid, info.qp_num, info.psn);

            qp_callback(std::move(qp), std::move(send_cq), std::move(recv_cq));
            return self_info;
        });
        svr.run();
    }

    void run_server_with_stop(qp_callback_with_stop_t qp_callback,
                              uint16_t port = kRpcPort) {
        hrpc::server svr{port};
        svr.bind(RPC_ESTABLISH, [this, qp_callback](hrpc::server *self,
                                                    rdma_rc_qp::info info) {
            rdma_cq send_cq{ctx}, recv_cq{ctx};
            rdma_rc_qp qp{ctx, send_cq, recv_cq, kQpDepth,
                          rdma_rc_qp::extended_atomics};
            qp.connect(info);

            auto self_info = qp.get_info();
            spdlog::trace(
                "connected local qp <gid {:x}-{:x}, lid {}, qpn {}, psn {}> to "
                "remote qp <gid {:x}-{:x}, lid {}, qpn {}, psn {}>",
                self_info.gid.global.subnet_prefix,
                self_info.gid.global.interface_id, self_info.lid,
                self_info.qp_num, self_info.psn, info.gid.global.subnet_prefix,
                info.gid.global.interface_id, info.lid, info.qp_num, info.psn);

            bool should_stop = qp_callback(std::move(qp), std::move(send_cq),
                                           std::move(recv_cq));
            if (should_stop) {
                self->stop();
            }
            return self_info;
        });
        svr.run();
    }

protected:
    static constexpr hrpc::hrpc_id_t RPC_ESTABLISH = 1;

    rdma_context const &ctx;
};

} // namespace rdmalib2

#endif // __RDMALIB2_CM_H__
