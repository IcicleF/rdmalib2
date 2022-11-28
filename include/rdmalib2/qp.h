#pragma once

#ifndef __RDMALIB2_QP_H__
#define __RDMALIB2_QP_H__

#include "context.h"
#include "cq.h"

namespace rdmalib2 {

class rdma_qp {
protected:
    template <ibv_qp_type Type> struct qp_type_base {};

public:
    template <ibv_qp_type T>
    rdma_qp(qp_type_base<T> const &type, int qp_depth, rdma_cq const &send_cq,
            rdma_cq const &recv_cq) {}
    ~rdma_qp() {}

    ibv_qp *get_qp() const { return qp; }

public:
    typedef qp_type_base<IBV_QPT_RAW_PACKET> raw_packet;
    typedef qp_type_base<IBV_QPT_RC> reliable_connection;
    typedef qp_type_base<IBV_QPT_UD> unreliable_datagram;
    typedef qp_type_base<IBV_QPT_XRC_SEND> xrc_send;
    typedef qp_type_base<IBV_QPT_XRC_RECV> xrc_recv;
    typedef qp_type_base<IBV_EXP_QPT_DC_INI> dc_initiator;

protected:
    ibv_qp *qp = nullptr;
};

} // namespace rdmalib2

#endif
