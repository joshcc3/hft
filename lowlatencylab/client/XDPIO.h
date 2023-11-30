//
// Created by joshuacoutinho on 29/11/23.
//

#ifndef XDPIO_H
#define XDPIO_H
#include <utility>
#include "../defs.h"
#include <sys/resource.h>
#include <sys/capability.h>
#include <sys/mman.h>

#include <xdp/xsk.h>
#include <xdp/libxdp.h>
#include <bpf/libbpf.h>

#include <linux/if_link.h>

#include <string>

#include "XDPIO.h"


struct XSKQueues {
    constexpr static int NUM_READ_DESC = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    constexpr static int NUM_WRITE_DESC = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    constexpr static int NUM_FILL_DESC = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2;
    constexpr static int NUM_COMPLETION_DESC = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    xsk_ring_cons completionQ{};
    xsk_ring_prod fillQ{};
    xsk_ring_prod txQ{};
    xsk_ring_cons rxQ{};

    void stateCheck() {
        assert(rxQ.mask == 0);
        assert(completionQ.mask != 0);
        assert(fillQ.mask != 0);
        assert(txQ.mask != 0);
    }
};

struct XSKUmem {
    constexpr static int NUM_FRAMES = 1 << 12;
    constexpr static int FRAME_SIZE = 1 << 10;

    xsk_umem* umem{};
    u8* buffer{};

    explicit XSKUmem(XSKQueues& qs) {
        constexpr int bufferSz = XSKUmem::NUM_FRAMES * XSKUmem::FRAME_SIZE;
        buffer = static_cast<u8 *>(mmap(nullptr, bufferSz,
                                        PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (buffer == MAP_FAILED) {
            printf("ERROR: mmap failed\n");
            exit(EXIT_FAILURE);
        }

        xsk_umem_config cfg = {
            .fill_size = XSKQueues::NUM_FILL_DESC,
            .comp_size = XSKQueues::NUM_COMPLETION_DESC,
            .frame_size = XSKUmem::FRAME_SIZE,
            .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
            .flags = 0
        };

        assert(umem.umem == nullptr);

        if (xsk_umem__create(&umem, buffer, bufferSz, &qs.fillQ, &qs.completionQ,
                             nullptr)) {
            perror("Umem: ");
            exit(EXIT_FAILURE);
        }
    }


    void stateCheck() {
        assert(buffer != nullptr);
    }
};


struct XSKSocket {
    constexpr static int QUEUE = 0;

    int xskFD{-1};
    xsk_socket* socket{};

    XSKSocket(const XSKUmem& umem, XSKQueues& qs, const std::string& iface) {
        xsk_socket_config cfg{};

        cfg.rx_size = XSKQueues::NUM_READ_DESC;
        cfg.tx_size = XSKQueues::NUM_WRITE_DESC;
        cfg.libxdp_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
        cfg.xdp_flags = XDP_FLAGS_SKB_MODE;
        cfg.bind_flags = XDP_USE_NEED_WAKEUP | XDP_COPY;

        if (int ret = xsk_socket__create(&socket, iface.c_str(), QUEUE, umem.umem,
                                         nullptr, &qs.txQ, &cfg)) {
            perror("XSK: ");
            exit(EXIT_FAILURE);
        }

        xskFD = xsk_socket__fd(socket);
    }

    void stateCheck() {
        assert(xskFD != -1);
        assert(socket != nullptr);
    }
};

struct RecvRes {
    u8 * readAddr;
    ssize_t len;
    const xdp_desc* readDesc;
};

class XDPIO {
    XSKQueues qs;
    XSKUmem umem;
    XSKSocket socket;

public:
    explicit XDPIO(const std::string& iface): qs{}, umem{qs}, socket{umem, qs, iface} {
        const rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
        if (setrlimit(RLIMIT_MEMLOCK, &r)) {
            cerr << "ERROR: setrlimit(RLIMIT_MEMLOCK): " << errno << endl;
            exit(EXIT_FAILURE);
        }
        setlocale(LC_ALL, "");
    }

    RecvRes recvBlocking() {
        qs.stateCheck();
        umem.stateCheck();
        socket.stateCheck();

        // if we get multiple out of order packets then we should process them smartly - last to first.
        u32 available;
        u32 idx;
        while ((available = xsk_ring_cons__peek(&qs.rxQ, XSKQueues::NUM_READ_DESC, &idx)) == 0) {}
        assert(available == 1);
        assert(xsk_cons_nb_avail(&qs.rxQ, XSKQueues::NUM_READ_DESC) == 0);
        assert(xsk_prod_nb_free(&qs.fillQ) == XSKQueues::NUM_FILL_DESC);
        assert(idx >= 0 && idx < XSKQueues::NUM_READ_DESC);

        // xsk_ring_cons;
        // xsk_ring_cons__comp_addr();
        // xsk_ring_cons__peek();
        // xsk_ring_cons__release();
        // xsk_ring_cons__rx_desc();
        //

        const xdp_desc* readDesc = xsk_ring_cons__rx_desc(&qs.rxQ, idx);
        __u64 addr = readDesc->addr;
        __u32 len = readDesc->len;
        __u32 options = readDesc->options;

        assert(xsk_umem__extract_addr(addr) == addr);
        assert(xsk_umem__extract_offset(addr) == 0);
        assert(options == 0);
        assert(umem.buffer & 4095 == 0);
        assert((addr & 255) == 0 && ((addr - 255) & (umem.FRAME_SIZE - 1)) == 0);
        assert(addr < umem.FRAME_SIZE * (umem.NUM_FRAMES - 1));
        assert(len < umem.FRAME_SIZE);

        u8* readAddr = static_cast<u8*>(xsk_umem__get_data(umem.buffer, addr));

        assert(readAddr == umem.buffer + addr);

        return {readAddr, len, readDesc};
    }

    void completeRead(const xdp_desc* readDesc) {
        if(readDesc != nullptr) {
            assert(readAddr >= umem.buffer && readAddr <= umem.buffer + XSKUmem::FRAME_SIZE * (XSKUmem::NUM_FRAMES - 1));
            xsk_ring_cons__release(&qs.rxQ, 1);

            assert(!xsk_ring_prod__needs_wakeup(&qs.fillQ));
            u32 idx;
            u32 reserved = xsk_ring_prod__reserve(&qs.fillQ, 1, &idx);
            assert(idx >= 0 && idx < XSKQueues::NUM_FILL_DESC);
            assert(reserved == 1);

            *xsk_ring_prod__fill_addr(&qs.fillQ, idx) = readDesc->addr;
            xsk_ring_prod__submit(&qs.fillQ, 1);
        } else {
#ifndef STRAT_XDP_FOR_XMIT
        assert(false);
#endif
        }
    }

    u8* getWriteBuff(ssize_t sz);

    void triggerWrite();
};

#endif //XDPIO_H
