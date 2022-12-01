#pragma once

#ifndef __RDMALIB2_QP_H__
#define __RDMALIB2_QP_H__

#include "predeclare/qp_pre.h"
#include "predeclare/verb_pre.h"

namespace rdmalib2 {

template <ibv_qp_type Type>
template <typename Wr>
void rdma_qp<Type>::post_verb(rdma_verb<Wr> &) {
    // TODO: implement this
}

template <ibv_qp_type Type>
template <typename ForwardIt>
void rdma_qp<Type>::post_verb(ForwardIt first, ForwardIt last) {
    // TODO: implement this
}

} // namespace rdmalib2

#endif // __RDMALIB2_QP_H__
