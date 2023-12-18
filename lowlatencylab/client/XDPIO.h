//
// Created by joshuacoutinho on 29/11/23.
//

#ifndef XDPIO_H
#define XDPIO_H
#include "defs.h"
#include <sys/resource.h>
#include <sys/capability.h>
#include <sys/mman.h>

#include <xdp/xsk.h>
#include <xdp/libxdp.h>
#include <linux/if_link.h>

#include "XDPIO.h"


struct XSKQueues {
    constexpr static int NUM_READ_DESC = XSK_RING_CONS__DEFAULT_NUM_DESCS * 8;
    constexpr static int NUM_WRITE_DESC = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    constexpr static int NUM_FILL_DESC = XSK_RING_PROD__DEFAULT_NUM_DESCS * 16;
    constexpr static int NUM_COMPLETION_DESC = XSK_RING_CONS__DEFAULT_NUM_DESCS;

    static_assert(__builtin_popcount(NUM_READ_DESC) == 1);
    static_assert(__builtin_popcount(NUM_WRITE_DESC) == 1);
    static_assert(__builtin_popcount(NUM_FILL_DESC) == 1);
    static_assert(__builtin_popcount(NUM_COMPLETION_DESC) == 1);

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

constexpr static int XSKUmem_FRAME_SIZE = XSK_UMEM__DEFAULT_FRAME_SIZE;

class UmemTxSlotState {
    constexpr static int MASK = XSKQueues::NUM_WRITE_DESC - 1;
    static_assert(__builtin_popcount(XSKQueues::NUM_WRITE_DESC) == 1);

public:
    std::bitset<XSKQueues::NUM_WRITE_DESC> umemFrameState{};
    int front{0};

    u32 nextSlot() {
        assert(front < XSKQueues::NUM_WRITE_DESC);
        const int nextSlot = front;
        assert(!umemFrameState.test(nextSlot));
        umemFrameState.set(nextSlot);
        front = front + 1 & MASK;
        return (nextSlot + XSKQueues::NUM_FILL_DESC) * XSKUmem_FRAME_SIZE;
    }

    void previousSlot(u32 addr) {
        front = (front + MASK) & MASK;
        assert(front == (addr / XSKUmem_FRAME_SIZE - XSKQueues::NUM_FILL_DESC));
        release(addr);
    }

    void release(u32 addr) {
        const u64 frameNo = addr / XSKUmem_FRAME_SIZE - XSKQueues::NUM_FILL_DESC;
        umemFrameState.reset(frameNo);
    }
};

struct XSKUmem {
    constexpr static int NUM_FRAMES = XSKQueues::NUM_FILL_DESC + XSKQueues::NUM_WRITE_DESC;

    UmemTxSlotState txState{};
    xsk_umem* umem{};
    u8* buffer{};


    explicit XSKUmem(XSKQueues& qs) {
        constexpr int bufferSz = XSKUmem::NUM_FRAMES * XSKUmem_FRAME_SIZE;
        buffer = static_cast<u8 *>(mmap(nullptr, bufferSz,
                                        PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
        if (buffer == MAP_FAILED) {
            printf__("ERROR: mmap failed\n");
            assert(false);
        }

        constexpr xsk_umem_config cfg = {
            .fill_size = XSKQueues::NUM_FILL_DESC,
            .comp_size = XSKQueues::NUM_COMPLETION_DESC,
            .frame_size = XSKUmem_FRAME_SIZE,
            .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
            .flags = 0
        };

        assert(umem == nullptr);

        if (xsk_umem__create(&umem, buffer, bufferSz, &qs.fillQ, &qs.completionQ,
                             &cfg)) {
            perror("Umem: ");
            assert(false);
        }
    }


    void stateCheck() const {
        assert(buffer != nullptr);
    }
};


struct XSKSocket {
    constexpr static int QUEUE = 0;

    xsk_socket_config cfg{};
    int xskFD{-1};
    xsk_socket* socket{};

    XSKSocket(const XSKUmem& umem, XSKQueues& qs, const std::string& iface, const std::string& xdpProgPinPath) {
        cfg.rx_size = XSKQueues::NUM_READ_DESC;
        cfg.tx_size = XSKQueues::NUM_WRITE_DESC;
        cfg.libxdp_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
        cfg.xdp_flags = XDP_FLAGS_DRV_MODE;
        cfg.bind_flags = XDP_USE_NEED_WAKEUP;

        const xdp_program* program = xdp_program__from_pin(xdpProgPinPath.c_str());
        assert (!(nullptr == program || u64(program) >= u64(-4095)));

        if (xsk_socket__create(&socket, iface.c_str(), QUEUE, umem.umem,
                               &qs.rxQ, &qs.txQ, &cfg)) {
            perror("XSK: ");
            assert(false);
        }

        xskFD = xsk_socket__fd(socket);
        assert(xskFD > 2);

        const int programFD = xdp_program__fd(program);
        assert(programFD > xskFD);
        const int xsksMapFD = lookupBPFMap(programFD);
        assert(xsksMapFD >= 0);

        constexpr int key = 0;
        const int fd = xskFD;
        const u32 ret = bpf_map_update_elem(xsksMapFD, &key, &fd, BPF_ANY);
        assert(ret == 0);


        int sock_opt = 1;
        if (setsockopt(xskFD, SOL_SOCKET, SO_PREFER_BUSY_POLL,
                       (void *) &sock_opt, sizeof(sock_opt)) < 0) {
            perror("No busy poll");
        }
        sock_opt = 100;
        if (setsockopt(xskFD, SOL_SOCKET, SO_BUSY_POLL,
                       (void *) &sock_opt, sizeof(sock_opt)) < 0) {
            perror("No busy poll");
            assert(false);
        }

        sock_opt = 1000;
        if (setsockopt(xskFD, SOL_SOCKET, SO_BUSY_POLL_BUDGET,
                       (void *) &sock_opt, sizeof(sock_opt)) < 0) {
            perror("No busy poll");
            assert(false);
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

            if (!strncmp(map_info.name, "redirMap", sizeof(map_info.name)) &&
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
    u32 available;
    u32 reserved;
    u32 idx;
    u32 fillQIdx;
};

class XDPIO {
public:
    int intransit = 0;
    XSKQueues qs;
    XSKUmem umem;
    XSKSocket socket;

    explicit XDPIO(const std::string& iface, const std::string& xdpProgPinPath): qs{}, umem{qs},
        socket{umem, qs, iface, xdpProgPinPath} {
        constexpr rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
        assert(!setrlimit(RLIMIT_MEMLOCK, &r)):
        setlocale(LC_ALL, "");

        qs.stateCheck();
        u32 idx = 0;
        const u32 prodReserveRes = xsk_ring_prod__reserve(&qs.fillQ, XSKQueues::NUM_FILL_DESC, &idx);
        assert(prodReserveRes == XSKQueues::NUM_FILL_DESC);
        assert(idx == 0);

        for (int i = 0; i < prodReserveRes; i++) {
            *xsk_ring_prod__fill_addr(&qs.fillQ, idx++) = i * XSKUmem_FRAME_SIZE;
        }
        xsk_ring_prod__submit(&qs.fillQ, XSKQueues::NUM_FILL_DESC);
    }

    template<bool Blocking>
    RecvRes recv() {
        static u32 idxStatic = 0;
        static u32 fillQIdxStatic = 0;
        RecvRes res{};
        res.idx = idxStatic;
        res.fillQIdx = fillQIdxStatic;
        if constexpr (Blocking) {
            while ((res.available = xsk_ring_cons__peek(&qs.rxQ, XSKQueues::NUM_READ_DESC, &res.idx)) == 0) {
            }
            while ((res.reserved = xsk_ring_prod__reserve(&qs.fillQ, res.available, &res.fillQIdx)) == 0) {
            }
        } else {
            for (int i = 0; i < 5; ++i) {
                res.available += xsk_ring_cons__peek(&qs.rxQ, XSKQueues::NUM_READ_DESC, &res.idx);
            }
            while (res.available != 0 && (res.reserved =
                                          xsk_ring_prod__reserve(&qs.fillQ, res.available, &res.fillQIdx)) == 0) {
            }
        }
        idxStatic = res.idx + res.available;
        fillQIdxStatic = res.fillQIdx + res.reserved;

        return res;
    }

    template<typename Handler>
    [[nodiscard]] ErrorCode handleFrames(u32 available, u32 reserved, u32 idx, u32 fillQIdx, const Handler& handler) {
        bool isAvail = available > 0;
        assert(reserved >= available);
        ErrorCode err{};
        const auto curTime = currentTimeNs();

        const xdp_desc* readDesc = xsk_ring_cons__rx_desc(&qs.rxQ, idx);
        const __u64 addr = readDesc->addr;
        const __u32 len = readDesc->len;
        const __u32 options = readDesc->options;

        assert(xsk_umem__extract_addr(addr) == addr);
        assert(xsk_umem__extract_offset(addr) == 0);
        assert(options == 0);
        assert((reinterpret_cast<u64>(umem.buffer) & 4095) == 0);
        assert((addr & 255) == 0 && ( (addr - 256) & (XSKUmem_FRAME_SIZE - 1)) == 0);
        assert(addr < XSKUmem_FRAME_SIZE * (umem.NUM_FRAMES - 1));
        assert(len < XSKUmem_FRAME_SIZE);

        const u8* readAddr = static_cast<u8 *>(xsk_umem__get_data(umem.buffer, addr));

        assert(readAddr == umem.buffer + addr);


        const u8* inBuf = readAddr;
        u32 bytesReadWithPhy = len;
        assert(inBuf != nullptr);

        assert(bytesReadWithPhy > 0);
        assert(bytesReadWithPhy <= XSKUmem_FRAME_SIZE);

        assert((reinterpret_cast<u64>(inBuf) & 127) == 0);

        const auto handlerErr = handler(inBuf, len, isAvail);
        err.append(handlerErr);

        for (int i = isAvail; __builtin_expect(i < available, true); ++i) {
            const auto curTime = currentTimeNs();

            const xdp_desc* readDesc = xsk_ring_cons__rx_desc(&qs.rxQ, idx + i);
            const __u64 addr = readDesc->addr;
            const __u32 len = readDesc->len;
            const __u32 options = readDesc->options;

            assert(xsk_umem__extract_addr(addr) == addr);
            assert(xsk_umem__extract_offset(addr) == 0);
            assert(options == 0);
            assert((reinterpret_cast<u64>(umem.buffer) & 4095) == 0);
            assert((addr & 255) == 0 && ( addr - 256 & XSKUmem_FRAME_SIZE - 1) == 0);
            assert(addr < XSKUmem_FRAME_SIZE * (umem.NUM_FRAMES - 1));
            assert(len < XSKUmem_FRAME_SIZE);

            const u8* readAddr = static_cast<u8 *>(xsk_umem__get_data(umem.buffer, addr));

            assert(readAddr == umem.buffer + addr);


            const u8* inBuf = readAddr;
            u32 bytesReadWithPhy = len;
            assert(inBuf != nullptr);

            assert(bytesReadWithPhy > 0);
            assert(bytesReadWithPhy <= XSKUmem_FRAME_SIZE);

            assert((reinterpret_cast<u64>(inBuf) & 127) == 0);

            const auto handlerErr = handler(inBuf, len, true);
            err.append(handlerErr);

            unsigned long long* fillQEntry = xsk_ring_prod__fill_addr(&qs.fillQ, fillQIdx + i);
            assert(fillQEntry != nullptr);
            *fillQEntry = readDesc->addr;
        }

        assert(!xsk_ring_prod__needs_wakeup(&qs.fillQ));
        xsk_ring_cons__release(&qs.rxQ, available);
        xsk_ring_prod__submit(&qs.fillQ, available);

        return err;
    }

    u8* getWriteBuff(ssize_t sz) {
        assert(intransit == 0);
        ++intransit;
        assert(sz < XSKUmem_FRAME_SIZE);
        u32 txIdx = -1;
        assert(qs.txQ.mask != 0);
        const u32 txSlotsRecvd = xsk_ring_prod__reserve(&qs.txQ, 1, &txIdx);
        if (__builtin_expect(txSlotsRecvd != 1, false)) {
            printf__("Could not reserve tx slots.");
            assert(false);
        }
        assert((txIdx & (XSKQueues::NUM_WRITE_DESC - 1)) == umem.txState.front);

        const u32 addr = umem.txState.nextSlot();

        xdp_desc* txDescr = xsk_ring_prod__tx_desc(&qs.txQ, txIdx);
        assert(nullptr != txDescr);
        txDescr->addr = addr;
        txDescr->len = sz;
        txDescr->options = 0;

        u8* writeBuf = umem.buffer + addr;
        return writeBuf;
    }

    void cancelPrevWriteBuff() {
        assert(intransit == 1);
        assert(qs.txQ.cached_prod <= qs.txQ.cached_cons);
        qs.txQ.cached_prod -= 1;
        xdp_desc* txDescr = xsk_ring_prod__tx_desc(&qs.txQ, qs.txQ.cached_prod);
        umem.txState.previousSlot(txDescr->addr);

        --intransit;
    }

    void triggerWrite() {
        assert(umem.txState.umemFrameState.any());
        assert(intransit == 1);
        --intransit;

        xsk_ring_prod__submit(&qs.txQ, 1);

        assert(((socket.cfg.bind_flags & XDP_COPY) != 0) == xsk_ring_prod__needs_wakeup(&qs.txQ));
        if (xsk_ring_prod__needs_wakeup(&qs.txQ)) {
            // TODO - this is onyl needed in COPY mode, not needed for zero-copy mode, driven by the napi loop
            assert(socket.xskFD > 2);
            assert((socket.cfg.bind_flags & XDP_COPY) != 0);
            const ssize_t ret = sendto(socket.xskFD, nullptr, 0, MSG_DONTWAIT, nullptr, 0);
            if (!(ret >= 0 || errno == ENOBUFS || errno == EAGAIN ||
                  errno == EBUSY || errno == ENETDOWN)) {
                perror("Kick did not work");
                assert(false);
            }
        } else {
            assert((socket.cfg.bind_flags & XDP_COPY) == 0);
        }
    }

    void complete() {
        u32 idx = -1;
        const u32 rcvd = xsk_ring_cons__peek(&qs.completionQ, XSKQueues::NUM_COMPLETION_DESC, &idx);
        if (rcvd > 0) {
            for (int i = 0; i < rcvd; ++i) {
                const __u64 addr = *xsk_ring_cons__comp_addr(&qs.completionQ, idx + i);
                umem.txState.release(addr);
            }
            xsk_ring_cons__release(&qs.completionQ, rcvd);
        }
    }

    std::pair<const xdp_desc *, u8 *> getReadDesc(u32 idx) {
        const xdp_desc* readDesc = xsk_ring_cons__rx_desc(&qs.rxQ, idx);
        const __u64 addr = readDesc->addr;
        const __u32 len = readDesc->len;
        const __u32 options = readDesc->options;

        assert(xsk_umem__extract_addr(addr) == addr);
        assert(xsk_umem__extract_offset(addr) == 0);
        assert(options == 0);
        assert((u64(umem.buffer) & 4095) == 0);
        assert((addr & 255) == 0 && ( addr - 256 & XSKUmem_FRAME_SIZE - 1) == 0);
        assert(addr < XSKUmem_FRAME_SIZE * (umem.NUM_FRAMES - 1));
        assert(len < XSKUmem_FRAME_SIZE);

        u8* readAddr = static_cast<u8 *>(xsk_umem__get_data(umem.buffer, addr));

        assert(readAddr == umem.buffer + addr);
        assert((reinterpret_cast<u64>(readAddr) & 127) == 0);

        return {readDesc, readAddr};
    }

    void releaseSingleFrame(u32 addr, unsigned fillQIdx) {
        unsigned long long* fillQEntry = xsk_ring_prod__fill_addr(&qs.fillQ, fillQIdx);
        assert(fillQEntry != nullptr);
        *fillQEntry = addr;

        assert(!xsk_ring_prod__needs_wakeup(&qs.fillQ));
        xsk_ring_cons__release(&qs.rxQ, 1);
        xsk_ring_prod__submit(&qs.fillQ, 1);
    }
};

#endif //XDPIO_H
