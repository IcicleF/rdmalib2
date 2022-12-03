#pragma once

#ifndef __RDMALIB2_VERB_H__
#define __RDMALIB2_VERB_H__

#include "predeclare/qp_pre.h"
#include "predeclare/verb_pre.h"

namespace rdmalib2 {

template <typename Wr>
template <ibv_qp_type Type>
void rdma_verb<Wr>::execute(rdma_qp<Type> const &qp) {
    qp.post_verb(*this);
}

} // namespace rdmalib2

#endif // __RDMALIB2_VERB_H__
