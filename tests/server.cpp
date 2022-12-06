#include <rdmalib2/rdmalib2.h>

int main(int argc, char **argv) {
    rdmalib2::rdma_context ctx;
    rdmalib2::cm cm{ctx};
}
