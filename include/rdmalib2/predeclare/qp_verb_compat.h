#pragma once

#ifndef __RDMALIB2_QP_VERB_COMPAT_H__
#define __RDMALIB2_QP_VERB_COMPAT_H__

#include <infiniband/verbs.h>
#include <type_traits>

namespace rdmalib2 {

template <ibv_qp_type Type> class rdma_qp;
template <typename Wr> class rdma_verb;

template <ibv_qp_type Type, typename Wr> struct qp_verb_compat;

template <> struct qp_verb_compat<IBV_QPT_RC, ibv_exp_send_wr> {
    constexpr bool operator()(ibv_exp_wr_opcode opcode) const {
        return opcode == IBV_EXP_WR_SEND ||
               opcode == IBV_EXP_WR_SEND_WITH_IMM ||
               opcode == IBV_EXP_WR_RDMA_WRITE ||
               opcode == IBV_EXP_WR_RDMA_WRITE_WITH_IMM ||
               opcode == IBV_EXP_WR_RDMA_READ ||
               opcode == IBV_EXP_WR_ATOMIC_CMP_AND_SWP ||
               opcode == IBV_EXP_WR_ATOMIC_FETCH_AND_ADD ||
               opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP ||
               opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD;
    }
};

template <> struct qp_verb_compat<IBV_QPT_UD, ibv_exp_send_wr> {
    constexpr bool operator()(ibv_exp_wr_opcode opcode) const {
        return opcode == IBV_EXP_WR_SEND || opcode == IBV_EXP_WR_SEND_WITH_IMM;
    }
};

template <> struct qp_verb_compat<IBV_QPT_RAW_PACKET, ibv_exp_send_wr> {
    constexpr bool operator()(ibv_exp_wr_opcode opcode) const {
        return opcode == IBV_EXP_WR_SEND || opcode == IBV_EXP_WR_SEND_WITH_IMM;
    }
};

template <ibv_qp_type Type, typename Wr> struct qp_verb_compat {
    constexpr bool operator()(ibv_exp_wr_opcode opcode) const { return false; }
};

} // namespace rdmalib2

#endif // __RDMALIB2_QP_VERB_COMPAT_H__
