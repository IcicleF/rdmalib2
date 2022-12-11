#include <catch2/catch_test_macros.hpp>

#include <rdmalib2/rdmalib2.h>

static constexpr size_t MEM_SIZE = 4096 * 1024;

TEST_CASE("rdmalib2 send/recv works normally", "rdmalib2") {
    spdlog::set_level(spdlog::level::trace);

    rdmalib2::rdma_context ctx{"mlx5_0"};
    rdmalib2::rdma_cq cq{ctx};
    rdmalib2::rdma_rc_qp qp{ctx, cq, cq};

    char *buf = new char[MEM_SIZE];
    for (int i = 0; i < MEM_SIZE; ++i) {
        buf[i] = i % 26 + 'a';
    }

    rdmalib2::rdma_memory_region mem{ctx, buf, MEM_SIZE};
    rdmalib2::rdma_memory_slice mslice{mem, 0, 1024};

    rdmalib2::cm cm{ctx};
    cm.connect(qp, "10.0.2.143");

    SECTION("send works normally") {
        rdmalib2::rdma_send_family wr{mslice};
        wr.set_op(rdmalib2::op_send).set_notified();
        wr.execute(qp);
        cq.poll();
    }

    delete[] buf;
}
