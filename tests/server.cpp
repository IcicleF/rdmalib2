#include <rdmalib2/rdmalib2.h>

static constexpr size_t MEM_SIZE = 4096 * 1024;

int main(int argc, char **argv) {
    rdmalib2::rdma_context ctx;
    rdmalib2::cm cm{ctx};

    char *buf = new char[MEM_SIZE];
    for (int i = 0; i < MEM_SIZE; ++i) {
        buf[i] = i % 26 + 'a';
    }

    rdmalib2::rdma_memory_region mem{ctx, buf, MEM_SIZE};
    rdmalib2::rdma_memory_slice mslice{mem, 0, 1024};

    std::optional<rdmalib2::rdma_rc_qp> qp_box;

    spdlog::info("server started");
    cm.run_server([&](rdmalib2::rdma_rc_qp qp, rdmalib2::rdma_cq send_cq,
                      rdmalib2::rdma_cq recv_cq) {
        rdmalib2::rdma_recv wr{mslice};
        wr.execute(qp);
    });

    return 0;
}
