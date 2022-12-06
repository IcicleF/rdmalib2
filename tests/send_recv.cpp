#include <catch2/catch_test_macros.hpp>

#include <rdmalib2/rdmalib2.h>

static constexpr size_t MEM_SIZE = 4096 * 1024;

TEST_CASE("rdmalib2 send/recv works normally", "rdmalib2") {
    rdmalib2::rdma_context ctx{"mlx5_0",
                               rdmalib2::rdma_context::thread_single{} +
                                   rdmalib2::rdma_context::msg_low_latency{}};
    rdmalib2::rdma_cq cq{ctx};
    rdmalib2::rdma_rc_qp qp{ctx, cq, cq};

    char *buf = new char[MEM_SIZE];
    rdmalib2::rdma_memory_region mem{ctx, buf, MEM_SIZE};

    rdmalib2::cm cm{ctx};
    cm.connect(qp, "10.0.2.144");

    // SECTION("send/recv works normally") {
    //     qp.post_recv(mem);
    //     qp.post_send(mem2);
    //     cq.wait();
    //     cq.wait();
    // }
}
