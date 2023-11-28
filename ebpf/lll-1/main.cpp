#include "defs.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cerrno>
#include <libgen.h>
#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <xdp/xsk.h>

#include <xdp/libxdp.h>
#include <map>
#include <bpf/bpf.h>


#include <cstdint>
#include <cassert>

using namespace std;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using Price = int;
using Qty = int;


// TODO - this doesn't seem to be using drv_mode - need to verify this.
// likely not using driver mode on the loopback interface - doesn't make sense.


constexpr inline int PACKET_TYPE_MD = 1;
constexpr inline int PACKET_TYPE_OE = 2;

struct market_data {
    int seqId;
    Price price;
    Qty qty;
    char side;
} __attribute__ ((packed));

struct PacketIn {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
    u8 packetType;
    char _padding[5];
    struct market_data md;
} __attribute__ ((packed));

struct order_data {
    int seqId;
    Price price;
    char side;
    int seqIdOut;
} __attribute__ ((packed));

struct PacketOut {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
    u8 packetType;
    char _padding[5];
    struct order_data od;
} __attribute__ ((packed));


struct xsk_ring_stats {
    unsigned long rx_frags;
    unsigned long rx_npkts;
    unsigned long tx_frags;
    unsigned long tx_npkts;
    unsigned long rx_dropped_npkts;
    unsigned long rx_invalid_npkts;
    unsigned long tx_invalid_npkts;
    unsigned long rx_full_npkts;
    unsigned long rx_fill_empty_npkts;
    unsigned long tx_empty_npkts;
    unsigned long prev_rx_frags;
    unsigned long prev_rx_npkts;
    unsigned long prev_tx_frags;
    unsigned long prev_tx_npkts;
    unsigned long prev_rx_dropped_npkts;
    unsigned long prev_rx_invalid_npkts;
    unsigned long prev_tx_invalid_npkts;
    unsigned long prev_rx_full_npkts;
    unsigned long prev_rx_fill_empty_npkts;
    unsigned long prev_tx_empty_npkts;
};

struct xsk_driver_stats {
    unsigned long intrs;
    unsigned long prev_intrs;
};

struct xsk_app_stats {
    unsigned long rx_empty_polls;
    unsigned long fill_fail_polls;
    unsigned long copy_tx_sendtos;
    unsigned long tx_wakeup_sendtos;
    unsigned long opt_polls;
    unsigned long prev_rx_empty_polls;
    unsigned long prev_fill_fail_polls;
    unsigned long prev_copy_tx_sendtos;
    unsigned long prev_tx_wakeup_sendtos;
    unsigned long prev_opt_polls;
};

struct XDPConfig {
    u16 bindFlags{};
    u32 xdpMode{};
    u32 libxdpFlags{};
};


static int lookup_bpf_map(int prog_fd) {
    u32 prog_len = sizeof(struct bpf_prog_info);
    u32 map_len = sizeof(struct bpf_map_info);

    int xsks_map_fd = -ENOENT;
    struct bpf_map_info map_info{};

    struct bpf_prog_info prog_info{};
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

        if (!strncmp(map_info.name, "xsks_map", sizeof(map_info.name)) &&
            map_info.key_size == 4 && map_info.value_size == 4) {
            xsks_map_fd = fd;
            break;
        }

        close(fd);
    }

    return xsks_map_fd;
}

class XSKUmem {
public:
    static_assert(sizeof(PacketIn) == sizeof(PacketOut));
    constexpr static int FRAME_SIZE = XSK_UMEM__DEFAULT_FRAME_SIZE;
    constexpr static int NUM_FRAMES = 4096;

    xsk_umem* umem{};
    u8* umemArea{};
    xsk_ring_prod fillQ{};
    xsk_ring_cons completionQ{};

    XSKUmem() {
        constexpr u64 size = NUM_FRAMES * FRAME_SIZE;
        umemArea = static_cast<u8 *>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
        if (umemArea == MAP_FAILED) {
            perror("UMEM mmap: ");
            exit(EXIT_FAILURE);
        }
        printf("Buffer addr: %lx\n", reinterpret_cast<u64>(umemArea));
        assert((reinterpret_cast<u64>(umemArea) & (getpagesize() - 1)) == 0);

        /*
            xsk_umem_config cfg = {
                .fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2,
                .comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
                .frame_size = FRAME_SIZE,
                .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
                .flags = XSK_UMEM__DEFAULT_FLAGS
            };
            This config doesn't seem to work properly.
        */
        const int res = xsk_umem__create(&umem,
                                         umemArea, size,
                                         &fillQ,
                                         &completionQ,
                                         nullptr);
        if (res != 0) {
            perror("Umem create: ");
            cerr << "Errno: " << -res << endl;
            exit(EXIT_FAILURE);
        }
        u32 idx = 0;
        const u32 prodReserveRes = xsk_ring_prod__reserve(&fillQ, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);
        if (prodReserveRes != XSK_RING_PROD__DEFAULT_NUM_DESCS) {
            cerr << "Fill ring reserve: " << -prodReserveRes << endl;
            exit(EXIT_FAILURE);
        }
        assert(idx == 0);

        for (int i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++) {
            *xsk_ring_prod__fill_addr(&fillQ, idx++) =
                    i * FRAME_SIZE;
        }
        xsk_ring_prod__submit(&fillQ, XSK_RING_PROD__DEFAULT_NUM_DESCS);

        assert(idx != 0);
    }
};

class XDPProgram {
public:
    xdp_program* program;

    XDPProgram() {
        program = xdp_program__from_pin("/sys/fs/bpf/lll_1");
        if (nullptr == program) {
            cerr << "Failed to get program" << endl;
            exit(EXIT_FAILURE);
        }
    }
};

class XSKSocket {
public:
    constexpr static int QUEUE_NUM = 0;
    XDPProgram& xdp_prog;

    XSKUmem& umem;
    xsk_socket* xsk{};
    xsk_ring_cons rx{};
    xsk_ring_prod tx{};
    xsk_ring_stats ring_stats{};
    xsk_app_stats app_stats{};
    xsk_driver_stats drv_stats{};
    u32 outstanding_tx{};
    int xskFD = -1;
    const XDPConfig& socketCfg;

    XSKSocket(XSKUmem& umem, XDPProgram& prog, const XDPConfig& socketCfg): xdp_prog{prog}, umem{umem},
                                                                            socketCfg{socketCfg} {
        xsk_socket_config cfg{
            .rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
            .tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
            .libxdp_flags = socketCfg.libxdpFlags,
            .xdp_flags = socketCfg.xdpMode,
            .bind_flags = socketCfg.bindFlags
        };

        int ret = xsk_socket__create(&xsk, "wlp0s20f3", QUEUE_NUM, umem.umem, &rx, &tx, &cfg);
        if (ret != 0) {
            cerr << "Errno: " << -ret << endl;
            exit(EXIT_FAILURE);
        }

        xskFD = xsk_socket__fd(xsk);
        assert(xskFD > 2 && xskFD < 20);

        assert(!IS_ERR_OR_NULL(xdp_prog.program));
        cout << "XSK Socket FD " << xskFD << endl;

        int programFD = xdp_program__fd(xdp_prog.program);
        int xsks_map = lookup_bpf_map(programFD);
        if (xsks_map < 0) {
            fprintf(stderr, "ERROR: no xsks map found: %s\n",
                    strerror(xsks_map));
            exit(EXIT_FAILURE);
        }

        int key = 0;
        int fd = xskFD;
        ret = bpf_map_update_elem(xsks_map, &key, &fd, BPF_ANY);
        if (ret != 0) {
            cerr << "bpf_map_update_elem Errno: " << -ret << endl;
            exit(EXIT_FAILURE);
        }


        assert(xskFD > 2);
        int sock_opt = 1;
        if (setsockopt(xskFD, SOL_SOCKET, SO_PREFER_BUSY_POLL,
                       (void *) &sock_opt, sizeof(sock_opt)) < 0) {
            perror("No busy poll");
            exit(EXIT_FAILURE);
        }

        sock_opt = 100;
        if (setsockopt(xskFD, SOL_SOCKET, SO_BUSY_POLL,
                       (void *) &sock_opt, sizeof(sock_opt)) < 0) {
            perror("No busy poll");
            exit(EXIT_FAILURE);
        }

        sock_opt = 1000;
        if (setsockopt(xskFD, SOL_SOCKET, SO_BUSY_POLL_BUDGET,
                       (void *) &sock_opt, sizeof(sock_opt)) < 0) {
            perror("No busy poll");
            exit(EXIT_FAILURE);
        }

        cout << "XSK init completed." << endl;
    }
};


class XDPManager {
public:
    XDPProgram program{};
    XSKUmem umem{};
    XSKSocket xsk;

    explicit XDPManager(const XDPConfig& cfg): xsk{umem, program, cfg} {
    }

    ~XDPManager() {
        exit(EXIT_FAILURE);
    }
};

class Sender {
public:
    static_assert(sizeof(PacketOut) <= XSKUmem::FRAME_SIZE);

    XSKSocket& sock;
    XSKUmem& umem;
    int seqNoOut;
    xsk_ring_prod* tx{};
    xsk_ring_cons* cq{};

    bool packetBuffered;


    int sockfd;
    sockaddr_in servaddr;
    const char* lastData = nullptr;


    explicit Sender(XDPManager& xdp): sock{xdp.xsk}, umem{xdp.umem}, seqNoOut{}, tx{&xdp.xsk.tx},
                                      cq{&xdp.umem.completionQ}, packetBuffered{false}, servaddr{} {
        // Create a socket
        //sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        //if (sockfd < 0) {
        //    std::cerr << "Socket creation failed" << std::endl;
        //    exit(EXIT_FAILURE);
        //}

        //// Fill in server information
        //memset(&servaddr, 0, sizeof(servaddr));
        //servaddr.sin_family = AF_INET;
        //servaddr.sin_port = htons(4321);
        //servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }


    void prepareIndependentPacket() {
        vector<u8> packetData = {
            0x45, 0x00, 0x00, 0x2f, 0xa9, 0x37, 0x40, 0x00, 0x40, 0x11,
            0x1c, 0x52, 0xc0, 0xa8, 0x00, 0x68,
            0x0d, 0x28, 0xa6, 0xfc, 0xb0, 0x82, 0x10, 0xe1, 0x00, 0x1b, 0x75, 0x61, 0x02, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x62, 0x00, 0x00, 0x00, 0x00
        };

    }

    void prepare(const xdp_desc* readDesc, char side, Price price, int seqNoIn) {
        assert(!packetBuffered);
        assert(readDesc->addr <= XSKUmem::FRAME_SIZE * (XSKUmem::NUM_FRAMES - 1));
        assert(readDesc->len > 0);
        assert(readDesc->options == 0);
        assert(seqNoOut < seqNoIn);


        u32 txIdx = -1;
        assert(tx != nullptr);
        const u32 txSlotsRecvd = xsk_ring_prod__reserve(tx, 1, &txIdx);
        assert(txIdx < XSK_RING_PROD__DEFAULT_NUM_DESCS);
        assert(txSlotsRecvd == 1);

        struct xdp_desc* txDescr = xsk_ring_prod__tx_desc(tx, txIdx);
        assert(nullptr != txDescr);
        txDescr->addr = readDesc->addr;
        txDescr->len = sizeof(PacketOut);
        txDescr->options = 0;
        PacketOut* umemLoc = reinterpret_cast<PacketOut *>(umem.umemArea + readDesc->addr);

        array<u8, ETH_ALEN> sourceMac = {0x3c, 0xe9, 0xf7, 0xfe, 0xdf, 0x6c};
        array<u8, ETH_ALEN> destMac = {0x48, 0xd3, 0x43, 0xe9, 0x5c, 0xa0};
        copy(sourceMac.begin(), sourceMac.end(), umemLoc->eth.h_source);
        copy(destMac.begin(), destMac.end(), umemLoc->eth.h_dest);

        //        assert(umemLoc->eth.h_dest[0] == 0 && umemLoc->eth.h_dest[1] == 0 && umemLoc->eth.h_dest[2] == 0 && umemLoc->eth.h_dest[3] == 0);
        //        assert(umemLoc->eth.h_source[0] == 0 && umemLoc->eth.h_source[1] == 0 && umemLoc->eth.h_source[2] == 0 && umemLoc->eth.h_source[3] == 0);
        assert(umemLoc->eth.h_proto == htons(ETH_P_IP));
        //assert(umemLoc->ip.frag_off == 0);
        assert(umemLoc->ip.ttl != 0);
        assert(umemLoc->ip.protocol == 17);
        assert(umemLoc->ip.tot_len == (htons(sizeof(PacketIn) - sizeof(ethhdr))));

        //	umemLoc->ip.frag_off = 0;
        umemLoc->ip.tos = 0; // TODO - set to higher value
        umemLoc->ip.tot_len = htons(sizeof(PacketOut) - sizeof(ethhdr));
        umemLoc->ip.id = htons(1);

        umemLoc->ip.ttl = static_cast<u8>(255);
        umemLoc->ip.check = 0;

        constexpr u8 sourceIPBytes[4] = {192, 168, 0, 104};
        constexpr u8 destIPBytes[4] = {13, 40, 166, 252};
        const u32 sourceIP = *reinterpret_cast<const u32*>(sourceIPBytes);
        const u32 destIP = *reinterpret_cast<const u32*>(destIPBytes);
        umemLoc->ip.saddr = sourceIP;
        umemLoc->ip.daddr = destIP;

        const u8* dataptr = reinterpret_cast<u8 *>(&umemLoc->ip);
        const u16 kernelcsum = ip_fast_csum(dataptr, umemLoc->ip.ihl);

        umemLoc->ip.check = kernelcsum;

        constexpr int udpPacketSz = sizeof(PacketOut) - sizeof(ethhdr) - sizeof(iphdr);
        umemLoc->udp.len = htons(udpPacketSz);
        umemLoc->udp.check = 0;
        umemLoc->udp.dest = htons(4321);
        umemLoc->udp.source = htons(1234);


        umemLoc->packetType = PACKET_TYPE_OE;

        umemLoc->od.seqId = seqNoIn;
        umemLoc->od.seqIdOut = seqNoOut;
        umemLoc->od.price = price;
        umemLoc->od.side = side;

        umemLoc->udp.check = 0;
        umemLoc->udp.check = udp_csum(umemLoc->ip.saddr, umemLoc->ip.daddr, umemLoc->udp.len,
                                      IPPROTO_UDP, reinterpret_cast<u16 *>(&umemLoc->udp));
        // Send an all 0 checksum to indicate no checksum was calculated
        // umemLoc->udp.check = 0;
        packetBuffered = true;
        ++seqNoOut;

        // vector<u8> packetData = {
        //     0x45, 0x00, 0x00, 0x2f, 0xc6, 0x93, 0x40, 0x00, 0x40, 0x11, 0x76, 0x28, 0x7f, 0x00,
        //     0x00, 0x01, 0x7f, 0x00, 0x00, 0x01, 0xb5, 0x57, 0x10, 0xe1, 0x00, 0x1b, 0xfe, 0x2e,
        //     0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        //     0x62, 0x00, 0x00, 0x00, 0x00
        // };


        // lastData = reinterpret_cast<char *>(&umemLoc->packetType);
        //hex_dump((const u8*)lastData, sizeof(order_data) + 6, 0);

        // copy(packetData.begin(), packetData.end(), reinterpret_cast<u8 *>(&umemLoc->ip));

        hex_dump(reinterpret_cast<const u8*>(umemLoc), sizeof(PacketOut), 0);
    }

    bool trigger() {
        if (packetBuffered) {
            xsk_ring_prod__submit(tx, 1);

            assert(((sock.socketCfg.bindFlags & XDP_COPY) != 0) == xsk_ring_prod__needs_wakeup(tx));
            if (xsk_ring_prod__needs_wakeup(tx)) {
                // TODO - this is onyl needed in COPY mode, not needed for zero-copy mode, driven by the napi loop
                assert(sock.xskFD > 2);
                assert((sock.socketCfg.bindFlags & XDP_COPY) != 0);
                const ssize_t ret = sendto(sock.xskFD, nullptr, 0, MSG_DONTWAIT, nullptr, 0);

                if (!(ret >= 0 || errno == ENOBUFS || errno == EAGAIN ||
                      errno == EBUSY || errno == ENETDOWN)) {
                    perror("Kick did not work");
                    exit(EXIT_FAILURE);
                }
            }

           // Data to be sent
//            assert(nullptr != lastData);
//            const ssize_t len = sendto(sockfd, lastData, sizeof(order_data) + 6, 0, reinterpret_cast<const struct sockaddr *>(&servaddr),
//                                       sizeof(servaddr));
//            if (len <= 0) {
//                std::cerr << "Send failed" << std::endl;
 //           }
            lastData = nullptr;
            packetBuffered = false;
            return true;
        } else {
            return false;
        }
    }

    void complete() const {
        u32 idx = -1;
        const u32 rcvd = xsk_ring_cons__peek(cq, 1, &idx);
        if (rcvd > 0) {
            cout << "Successfully completed" << endl;
            xsk_ring_cons__release(cq, rcvd);
            // TODO - release to the fill queue over here as well
        }
    }

    void stateCheck() const {
        assert(tx != nullptr);
        assert(cq != nullptr);
    }
};


class OB {
public:
    map<Price, Qty> bids;
    map<Price, Qty> asks;
    Sender& s;

    explicit OB(Sender& s): s{s} {}

    void update(const xdp_desc* readDesc, const PacketIn& packet) {
        market_data p = packet.md;
        if (p.side == 'b') {
            if (p.qty == 0) {
                bids.erase(p.price);
            } else {
                if (p.price >= bids.begin()->first && p.qty > 1000) {
                    s.prepare(readDesc, p.side, p.price, p.seqId);
                }
                const auto& [iter, added] = bids.emplace(p.price, p.qty);
                iter->second = p.qty;
            }
        } else if (p.side == 'a') {
            if (p.qty == 0) {
                asks.erase(p.price);
            } else {
                if (p.price <= asks.begin()->first && p.qty > 1000) {
                    s.prepare(readDesc, p.side, p.price, p.seqId);
                }
                const auto& [iter, added] = asks.emplace(p.price, p.qty);
                iter->second = p.qty;
            }
        } else {
            assert(false);
        }
    }
};


class Receiver {
public:
    XDPManager& xdp;
    OB& ob;
    Sender& s;
    constexpr static u32 batchSize = 64;

    Receiver(XDPManager& xdp, OB& ob, Sender& s): xdp{xdp}, ob{ob}, s{s} {
    }

    void recv() const {
        u32 idxRx = 0;
        u32 entries;
        assert(!xsk_ring_prod__needs_wakeup(&xdp.umem.fillQ));
        while ((entries = xsk_ring_cons__peek(&xdp.xsk.rx, batchSize, &idxRx)) == 0){}
        assert(entries > 0);
        assert(idxRx < XSK_RING_CONS__DEFAULT_NUM_DESCS);

        u32 idxFillQ = 0;
        u32 ret = xsk_ring_prod__reserve(&xdp.umem.fillQ, entries, &idxFillQ);
        while (ret != entries) {
            ret = xsk_ring_prod__reserve(&xdp.umem.fillQ, entries, &idxFillQ);
        }
        cout << "Received [" << entries << "]." << endl;
        for (int i = 0; i < entries; ++i) {
            const xdp_desc* desc = xsk_ring_cons__rx_desc(&xdp.xsk.rx, idxRx++);
            const u64 addrOffset = desc->addr;
            const u32 len = desc->len;
            assert(addrOffset == xsk_umem__extract_addr(addrOffset));
            assert(len == sizeof(PacketIn));

            assert(xdp.umem.umemArea + addrOffset == xsk_umem__get_data(xdp.umem.umemArea, addrOffset));
            u8* packetData = static_cast<u8 *>(xsk_umem__get_data(xdp.umem.umemArea, addrOffset));
            assert((reinterpret_cast<u64>(packetData) & 127) == 0);
            const PacketIn* packet = reinterpret_cast<PacketIn *>(packetData);
            hex_dump(packetData, sizeof(PacketIn), desc->addr);

            assert(packet->eth.h_proto == htons(ETH_P_IP));
            assert(packet->ip.version == 4);
            assert(packet->ip.ihl == 5);
            assert(htons(packet->ip.tot_len) == sizeof(PacketIn) - sizeof(ethhdr));
            //assert((packet->ip.frag_off & 0x1fff) == 0);
            assert(((packet->ip.frag_off >> 13) & 1) == 0);
            assert(((packet->ip.frag_off >> 15) & 1) == 0);
            assert(htons(packet->udp.len) == htons(packet->ip.tot_len) - sizeof(iphdr));
            assert(packet->packetType == PACKET_TYPE_MD);
            ob.update(desc, *packet);
            if (!s.packetBuffered) {
                *xsk_ring_prod__fill_addr(&xdp.umem.fillQ, idxFillQ++) = addrOffset;
            }
        }
        if (!s.trigger()) {
            // TODO - update the fill q on a sender complete to indicate can be used for receiving
            xsk_ring_prod__submit(&xdp.umem.fillQ, entries);
        }
        xsk_ring_cons__release(&xdp.xsk.rx, entries);
        xdp.xsk.ring_stats.rx_frags += entries;
    }
};


[[noreturn]] int main(int argc, char** argv) {
    // TODO - verify that these flags actually work.
    XDPConfig cfg{
        .bindFlags = XDP_COPY & (~XDP_USE_NEED_WAKEUP),
        .xdpMode = XDP_FLAGS_SKB_MODE, // XDP_FLAGS_DRV_MODE,
        .libxdpFlags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD,
    };
    XDPManager xdp{cfg};

    Sender s{xdp};
    OB ob(s);
    Receiver r(xdp, ob, s);

    while (true) {
        r.recv();
        s.complete();
    }
}
