#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/limits.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <locale.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <iostream>
#include <xdp/xsk.h>
#include <bpf/xsk.h>

#include <xdp/libxdp.h>
#include <map>
#include <bpf/libbpf.h>
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

struct data_in {
    int seqId;
    char side;
    Price price;
    Qty qty;
};

struct PacketIn {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
    struct data_in dat;
};


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


static int lookup_bpf_map(int prog_fd)
{
  u32 i;

   u32 num_maps;
  u32 prog_len = sizeof(struct bpf_prog_info);
	u32 map_len = sizeof(struct bpf_map_info);

	int fd, err, xsks_map_fd = -ENOENT;
	struct bpf_map_info map_info{};

	struct bpf_prog_info prog_info{};
	err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &prog_len);
	if (err)
	  return err;

	num_maps = prog_info.nr_map_ids;
	u32 map_ids[num_maps];


	memset(&prog_info, 0, prog_len);
	prog_info.nr_map_ids = num_maps;
	prog_info.map_ids = (__u64)(unsigned long)map_ids;

	err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &prog_len);
	if (err) {
		free(map_ids);
		return err;
	}

	for (i = 0; i < prog_info.nr_map_ids; i++) {
		fd = bpf_map_get_fd_by_id(map_ids[i]);
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

    constexpr static int FRAME_SIZE = XSK_UMEM__DEFAULT_FRAME_SIZE;
    constexpr static int NUM_FRAMES = 4096;

    xsk_umem *umem{};
    u8* umemArea{};
    xsk_ring_prod fillQ{};
    xsk_ring_cons completionQ{};

    XSKUmem() {
        u64 size = NUM_FRAMES * FRAME_SIZE;
        umemArea = static_cast<u8*>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
        if(umemArea == MAP_FAILED) {
            perror("UMEM mmap: ");
            exit(EXIT_FAILURE);
        }
	    printf("Buffer addr: %lx\n", u64(umemArea));
	    assert((u64(umemArea) & (getpagesize() - 1)) == 0);

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
        int res = xsk_umem__create(&umem,
                             umemArea, size,
                             &fillQ,
                             &completionQ,
			                 NULL);
        if(res != 0) {
	        perror("Umem create: " );
            cerr << "Errno: " << -res << endl;
            exit(EXIT_FAILURE);
        }
        u32 idx = 0;
        res = xsk_ring_prod__reserve(&fillQ, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);
        if(res != XSK_RING_PROD__DEFAULT_NUM_DESCS) {
            cerr << "Fill ring reserve: " << -res << endl;
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
	if(NULL == program) {
	  cerr << "Failed to get program" << endl;
	  exit(EXIT_FAILURE);
	}
    }

    ~XDPProgram() {
    }
};

class XSKSocket {
public:
    constexpr static int QUEUE_NUM = 0;
    XDPProgram& xdp_prog;

    XSKUmem& umem;
    xsk_socket *xsk{};
    xsk_ring_cons rx{};
    xsk_ring_prod tx{};
    xsk_ring_stats ring_stats{};
    xsk_app_stats app_stats{};
    xsk_driver_stats drv_stats{};
    u32 outstanding_tx{};
    int xskFD = -1;

    int if_index = if_nametoindex("lo");

    XSKSocket(XSKUmem& umem, XDPProgram& prog): umem{umem}, xdp_prog{prog} {

        u16 bindFlags = XDP_COPY; // TODO - change to use zero copy
        bindFlags &= ~XDP_USE_NEED_WAKEUP; // do not handle partial frames
        xsk_socket_config cfg {
            .rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
            .tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
            .libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD,
            .xdp_flags = XDP_FLAGS_DRV_MODE,
            .bind_flags = bindFlags
        };

        int ret = xsk_socket__create(&xsk, "lo", QUEUE_NUM, umem.umem, &rx, &tx, &cfg);
        if(ret != 0) {
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
	cout << "XSK init completed." << endl;

    }

};


class XDPManager {
public:
  XDPProgram program{};
  XSKUmem umem{};
  XSKSocket xsk;

  XDPManager(): xsk{umem, program} {

  }

  ~XDPManager() {
            exit(EXIT_FAILURE);
  }
};

struct Payload {
    int seqNoIn;
    int seqNoOut;
    Price price;
    char side;
}
struct PacketOut {
    ethhdr eth;
    iphdr ip;
    udphdr udp;
    Payload payload;
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

    Sender(XDPManager& xdp):
        sock{xdp.socket}, umem{xdp.umem}, seqNoOut{}, packetBuffered{false},
        cq{&xdp.umem.completionQ}, tx{&xdp.socket.tx}    {
    }

    void prepare(xdp_desc* readDesc, char side, Price price, int seqNoIn) {
        assert(!packetBuffered);
        assert(readDesc->addr <= XSKUmem::FRAME_SIZE * (XSKUmem::NUM_FRAMES - 1));
        assert(readDesc->len > 0);
        assert(readDesc->options == 0);
        assert(seqNoOut < seqNoIn);


        int txIdx = -1;
        assert(tx != null && tx >= 0x7fffff0000000000);
        int txSlotsRecvd = xsk_ring_prod__reserve(tx, 1, &txIdx);
        assert(txIdx >= 0 && txIdx < XSK_RING_PROD__DEFAULT_NUM_DESCS);
        assert(txSlotsRecvd == 1);

        struct xdp_desc *txDescr = xsk_ring_prod__tx_desc(tx, txIdx);
        assert(nullptr != txDescr);
        txDescr->addr = readDesc->addr;
        txDescr->len = sizeof(PacketOut);
        txDescr->options = 0;

        PacketOut* umemLoc = reinterpret_cast<PacketOut*>(umem.umemArea + readDesc->addr * FRAME_SIZE);
        assert(umemLoc->eth.h_dest[0] == 0 && umemLoc->eth.h_dest[1] == 0 && umemLoc->eth.h_dest[2] == 0 && umemLoc->eth.h_dest[3] == 0);
        assert(umemLoc->eth.h_source[0] == 0 && umemLoc->eth.h_source[1] == 0 && umemLoc->eth.h_source[2] == 0 && umemLoc->eth.h_source[3] == 0);
        assert(umemLoc->eth.h_proto == htons(ETH_P_IP));

        assert(umemLoc.ip.frag_off == 0);
        assert(umemLoc.ip.ttl != 0);
        assert(umemLoc.ip.protocol == 17);

        umemLoc.ip.totlen = htons(sizeof(PacketOut));
        umemLoc.ip.ttl = htons(255);

        umemLoc.ip.check = 0;
        u32 csum = 0;
        u16* dataptr = reinterpret_cast<u16*>(&umemLoc.ip);
        for(int i = 0; i < sizeof(iphdr)/sizeof(u16); ++i) {
            csum += dataptr[i];
        }
        csum = (csum & 0xffff) + (csum >> 16);
        csum = (csum & 0xffff) + (csum >> 16);
        umemLoc.ip.check = ~u16(csum);

        constexpr int udpPacketSz = sizeof(udphdr) + sizeof(Payload);
        umemLoc.len = udpPacketSz;
        umemLoc.check = 0;

        // TODO - swap udp header source and destination as well.
        umemLoc.payload.seqNoIn = seqNoIn;
        umemLoc.payload.seqNoOut = seqNoOut;
        umemLoc.payload.price = price;
        umemLoc.payload.side = side;

        csum = 0;
        dataptr = reinterpret_cast<u16*>(&umemLoc.udp);
        for(int i = 0; i < udpPacketSz / sizeof(u16); ++i) {
            csum += dataptr[i];
        }
        csum = (csum & 0xffff) + (csum >> 16);
        csum = (csum & 0xffff) + (csum >> 16);
        umemLoc.udp.check = ~u16(csum);
        packetBuffered = true;
        ++seqNo;
    }

    void trigger() {
        if(packetBuffered) {
            xsk_ring_prod__submit(tx, 1);
            packetBuffered = false;
            return true;
        } else {
            return false;
        }
    }

    void complete() {
        int idx = -1;
        rcvd = xsk_ring_cons__peek(cq, 1, &idx);
        assert(recvd >= 0);
        assert(idx >= 0);
        if (rcvd > 0) {
            xsk_ring_cons__release(cq, rcvd);
        }
    }

    void stateCheck() {
        assert(tx != nullptr);
        assert(cq != nullptr);
    }

};



class OB {
public:
    map<Price, Qty> bids;
    map<Price, Qty> asks;
    Sender s;
    OB(const Sender& s): s{s}, bids{}, asks{} {

    }

    void update(struct xdp_desc* readDesc, const PacketIn& packet) {
        struct data_in p = packet.dat;
        if(p.side == 'b') {
            if(p.qty == 0) {
               bids.erase(p.price);
            } else {
                if(p.price >= bids.begin()->first && p.qty > 1000) {
                    s.prepare(readDesc, p.side, p.price, p.seqId)
                }
                const auto& [iter, added] = bids.emplace(p.price, p.qty);
                iter->second = p.qty;
            }
        } else if(p.side == 'a') {
            if(p.qty == 0) {
                asks.erase(p.price);
            } else {
                if(p.price <= asks.begin()->first && p.qty > 1000) {
                    s.prepare(readDesc, p.side, p.price, p.seqId)
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

    Receiver(XDPManager& xdp, OB& ob, Sender& s): xdp{xdp}, ob{ob}, s{s} {}

    void recv() {
        u32 idxRx = 0;
        u32 entries = 0;
        assert(!xsk_ring_prod__needs_wakeup(&xdp.umem.fillQ));
        while((entries = xsk_ring_cons__peek(&xdp.xsk.rx, batchSize, &idxRx)) == 0);
        assert(entries > 0);
        assert(idxRx >= 0 && idxRx < XSK_RING_CONS__DEFAULT_NUM_DESCS);

        u32 idxFillQ = 0;
        int ret = xsk_ring_prod__reserve(&xdp.umem.fillQ, entries, &idxFillQ);
        while(ret != entries) {
            if(ret < 0) {
                cerr << "Could not reserve entries: " << -ret << endl;
                exit(EXIT_FAILURE);
            }
            ret = xsk_ring_prod__reserve(&xdp.umem.fillQ, entries, &idxFillQ);
        }
	    cout << "Received [" << entries << "]." << endl;
        for(int i = 0; i < entries; ++i) {
            const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xdp.xsk.rx, idxRx++);
            u64 addrOffset = desc->addr;
            u32 len = desc->len;
            assert(addrOffset == xsk_umem__extract_addr(addrOffset));

    	    const PacketIn* packet = static_cast<PacketIn*>(xdp.umem.umemArea + addrOffset);
            ob.update(desc, *packet);
            if(!s.packetBuffered) {
                *xsk_ring_prod__fill_addr(&xdp.umem.fillQ, idxFillQ++) = addrOffset;
            }
        }
        if(!s.trigger()) {
            xsk_ring_prod__submit(&xdp.umem.fillQ, entries);
        }
        xsk_ring_cons__release(&xdp.xsk.rx, entries);
        xdp.xsk.ring_stats.rx_frags += entries;

    }


};



int main(int argc, char **argv) {

    XDPManager xdp;

    Sender s;
    OB ob(s);
    Receiver r(xdp, ob);

    while(true) {
        r.recv();
        s.complete();
    }

}
