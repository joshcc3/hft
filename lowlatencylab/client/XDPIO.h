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
    constexpr static int NUM_READ_DESC = XSK_RING_CONS__DEFAULT_NUM_DESCS * 8;
    constexpr static int NUM_WRITE_DESC = XSK_RING_PROD__DEFAULT_NUM_DESCS * 8;
    constexpr static int NUM_FILL_DESC = XSK_RING_PROD__DEFAULT_NUM_DESCS * 16;
    constexpr static int NUM_COMPLETION_DESC = XSK_RING_CONS__DEFAULT_NUM_DESCS * 8;
    xsk_ring_cons completionQ{};
    xsk_ring_prod fillQ{};
    xsk_ring_prod txQ{};
    xsk_ring_cons rxQ{};

    void stateCheck() {
        assert(rxQ.mask != 0);
        assert(completionQ.mask != 0);
        assert(fillQ.mask != 0);
        assert(txQ.mask != 0);
    }
};

struct XSKUmem {
    constexpr static int NUM_FRAMES = 1 << 14;
    constexpr static int FRAME_SIZE = 1 << 12;

    xsk_umem* umem{};
    u8* buffer{};

    explicit XSKUmem(XSKQueues& qs) {
        constexpr int bufferSz = XSKUmem::NUM_FRAMES * XSKUmem::FRAME_SIZE;
        buffer = static_cast<u8 *>(mmap(nullptr, bufferSz,
                                        PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
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

        assert(umem == nullptr);

        if (xsk_umem__create(&umem, buffer, bufferSz, &qs.fillQ, &qs.completionQ,
                             &cfg)) {
            perror("Umem: ");
            exit(EXIT_FAILURE);
        }
    }


    void stateCheck() const {
        assert(buffer != nullptr);
    }
};


struct XSKSocket {
    constexpr static int QUEUE = 0;

    int xskFD{-1};
    xsk_socket* socket{};

    XSKSocket(const XSKUmem& umem, XSKQueues& qs, const std::string& iface, const std::string& xdpProgPinPath) {
        xsk_socket_config cfg{};

        cfg.rx_size = XSKQueues::NUM_READ_DESC;
        cfg.tx_size = XSKQueues::NUM_WRITE_DESC;
        cfg.libxdp_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
        cfg.xdp_flags = XDP_FLAGS_SKB_MODE;
        cfg.bind_flags = XDP_USE_NEED_WAKEUP | XDP_COPY;

        if (int ret = xsk_socket__create(&socket, iface.c_str(), QUEUE, umem.umem,
                                         &qs.rxQ, &qs.txQ, &cfg)) {
            perror("XSK: ");
            exit(EXIT_FAILURE);
        }

        xskFD = xsk_socket__fd(socket);
        assert(xskFD > 2);

        const xdp_program* program = xdp_program__from_pin(xdpProgPinPath.c_str());
        if (nullptr == program) {
            cerr << "Failed to get program" << endl;
            exit(EXIT_FAILURE);
        }

        const int programFD = xdp_program__fd(program);
        assert(programFD > xskFD);
        const int xsksMapFD = lookupBPFMap(programFD);
        if (xsksMapFD < 0) {
            cerr << "ERROR: no xsks map found: " << xsksMapFD << endl;
            exit(EXIT_FAILURE);
        }

        const int key = 0;
        const int fd = xskFD;
        const u32 ret = bpf_map_update_elem(xsksMapFD, &key, &fd, BPF_ANY);
        if (ret != 0) {
            cerr << "bpf_map_update_elem Errno: " << -ret << endl;
            exit(EXIT_FAILURE);
        }
    }

    void stateCheck() const {
        assert(xskFD != -1);
        assert(socket != nullptr);
    }


    static int lookupBPFMap(int prog_fd) {
        u32 prog_len = sizeof(struct bpf_prog_info);
        u32 map_len = sizeof(struct bpf_map_info);

        int xsks_map_fd = -ENOENT;
        bpf_map_info map_info{};

        bpf_prog_info prog_info{};
        int err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &prog_len);
        if (err)
            return err;

        const u32 num_maps = prog_info.nr_map_ids;
        u32 map_ids[num_maps];


        memset(&prog_info, 0, prog_len);
        prog_info.nr_map_ids = num_maps;
        prog_info.map_ids = reinterpret_cast<u64>(map_ids);

        err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &prog_len);
        if (err) {
            return err;
        }

        for (u32 i = 0; i < prog_info.nr_map_ids; i++) {
            const int fd = bpf_map_get_fd_by_id(map_ids[i]);
            if (fd < 0)
                continue;

            memset(&map_info, 0, map_len);
            err = bpf_obj_get_info_by_fd(fd, &map_info, &map_len);
            if (err) {
                close(fd);
                continue;
            }

            if (!strncmp(map_info.name, "mdRedirMap", sizeof(map_info.name)) &&
                map_info.key_size == 4 && map_info.value_size == 4) {
                xsks_map_fd = fd;
                break;
            }

            close(fd);
        }

        return xsks_map_fd;
    }
};


struct RecvRes {
    u8* readAddr;
    ssize_t len;
    const xdp_desc* readDesc;
};


class XDPIO {
public:
    XSKQueues qs;
    XSKUmem umem;
    XSKSocket socket;

    explicit XDPIO(const std::string& iface, const std::string& xdpProgPinPath): qs{}, umem{qs}, socket{umem, qs, iface, xdpProgPinPath} {
        const rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
        if (setrlimit(RLIMIT_MEMLOCK, &r)) {
            cerr << "ERROR: setrlimit(RLIMIT_MEMLOCK): " << errno << endl;
            exit(EXIT_FAILURE);
        }
        setlocale(LC_ALL, "");

        qs.stateCheck();
        u32 idx = 0;
        const u32 prodReserveRes = xsk_ring_prod__reserve(&qs.fillQ, XSKQueues::NUM_FILL_DESC, &idx);
        if (prodReserveRes != XSKQueues::NUM_FILL_DESC) {
            cerr << "Fill ring reserve: " << -prodReserveRes << endl;
            exit(EXIT_FAILURE);
        }
        assert(idx == 0);

        for (int i = 0; i < prodReserveRes; i++) {
            *xsk_ring_prod__fill_addr(&qs.fillQ, idx++) = i * XSKUmem::FRAME_SIZE;
        }
        xsk_ring_prod__submit(&qs.fillQ, XSKQueues::NUM_FILL_DESC);
    }

    u8* getWriteBuff(ssize_t sz);

    void triggerWrite();
};

#endif //XDPIO_H
