#pragma once

#ifndef __RDMALIB2_QP_H__
#define __RDMALIB2_QP_H__

#include "predeclare/qp_pre.h"
#include "predeclare/qp_verb_compat.h"
#include "predeclare/verb_pre.h"

namespace rdmalib2 {

template <ibv_qp_type Type>
template <typename Wr>
void rdma_qp<Type>::post_verb(rdma_verb<Wr> &verb) {
    static_assert(std::is_same_v<Wr, ibv_exp_send_wr> ||
                      std::is_same_v<Wr, ibv_recv_wr>,
                  "Unknown work request type");
    RDMALIB2_ASSERT(qp_verb_compat<Type, Wr>{}(verb.opcode()));

    if constexpr (std::is_same_v<Wr, ibv_exp_send_wr>) {
    }

    if constexpr (std::is_same_v<Wr, ibv_recv_wr>) {
    }
}

template <ibv_qp_type Type>
template <typename ForwardIt>
void rdma_qp<Type>::post_verb(ForwardIt first, ForwardIt last) {
    // TODO: implement this
}

} // namespace rdmalib2

#endif // __RDMALIB2_QP_H__
