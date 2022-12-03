#pragma once

#ifndef __RDMALIB2_QP_H__
#define __RDMALIB2_QP_H__

#include "predeclare/qp_pre.h"
#include "predeclare/qp_verb_compat.h"
#include "predeclare/verb_pre.h"

namespace rdmalib2 {

template <ibv_qp_type Type>
template <typename Wr>
void rdma_qp<Type>::post_verb(rdma_verb<Wr> &verb) const {
    static_assert(std::is_same_v<Wr, ibv_exp_send_wr> ||
                      std::is_same_v<Wr, ibv_recv_wr>,
                  "Unknown work request type");
    RDMALIB2_ASSERT(verb.get_op().has_value());
    RDMALIB2_ASSERT((qp_verb_compat<Type, Wr>{})(*(verb.get_op())));
    int ret = 0;
    Wr *bad_wr = nullptr;

    if constexpr (std::is_same_v<Wr, ibv_exp_send_wr>) {
        ret = ibv_exp_post_send(qp, &verb.get_wr(), &bad_wr);
    }
    if constexpr (std::is_same_v<Wr, ibv_recv_wr>) {
        ret = ibv_post_recv(qp, &verb.get_wr(), &bad_wr);
    }

    if (unlikely(ret)) {
        spdlog::error("post {} failed with return value {}",
                      std::is_same_v<Wr, ibv_exp_send_wr> ? "send" : "recv",
                      ret);
        panic_with_errno();
    }
}

template <ibv_qp_type Type>
template <typename ForwardIt>
void rdma_qp<Type>::post_verb(ForwardIt first, ForwardIt last) const {
    using Wr =
        typename std::remove_cv_t<std::decay_t<decltype(*first)>>::wr_type;
    static_assert(std::is_same_v<Wr, ibv_exp_send_wr> ||
                      std::is_same_v<Wr, ibv_recv_wr>,
                  "Unknown work request type");

    // Temporarily chain the work requests together
    for (ForwardIt it = first; it != last; ++it) {
        RDMALIB2_ASSERT((*it).get_op().has_value());
        RDMALIB2_ASSERT((qp_verb_compat<Type, Wr>{})(*((*it).get_op())));

        auto next = std::next(it);
        (*it).get_wr();
        if (next != last) {
            (*it).set_next(*next);
        } else {
            (*it).clear_next();
        }
    }

    int ret = 0;
    Wr *bad_wr = nullptr;

    if constexpr (std::is_same_v<Wr, ibv_exp_send_wr>) {
        ret = ibv_exp_post_send(qp, &(*first).get_wr(), &bad_wr);
    }
    if constexpr (std::is_same_v<Wr, ibv_recv_wr>) {
        ret = ibv_post_recv(qp, &(*first).get_wr(), &bad_wr);
    }

    if (unlikely(ret)) {
        spdlog::error("post {} failed with return value {}",
                      std::is_same_v<Wr, ibv_exp_send_wr> ? "send" : "recv",
                      ret);
        panic_with_errno();
    }

    // Cleanup
    for (ForwardIt it = first; it != last; ++it) {
        (*it).clear_next();
    }
}

} // namespace rdmalib2

#endif // __RDMALIB2_QP_H__
