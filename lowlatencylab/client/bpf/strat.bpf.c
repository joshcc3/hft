// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_xdp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/limits.h>
#include <linux/udp.h>
#include <linux/tcp.h>

/* This XDP program is only needed for multi-buffer and XDP_SHARED_UMEM modes.
 * If you do not use these modes, libbpf can supply an XDP program for you.
 */

#define htons(x) ((__be16)___constant_swab16((x)))
#define htonl(x) ((__be32)___constant_swab32((x)))

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} redirMap SEC(".maps");


typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

u16 OE_PORT = 9012;
u16 MD_UNICAST_PORT = 4321;

struct UDPPacket {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
    u8 packetType;
    char _padding[5];
} __attribute__ ((packed));

struct TCPPacket {
    struct ethhdr eth;
    struct iphdr ip;
    struct tcphdr tcp;
} __attribute__ ((packed));

int num_socks = 1;
int counter = 0;


SEC("xdp")
int strat(struct xdp_md *ctx)
{

  if(ctx->data != ctx->data_meta) {
    bpf_printk("Unexpected metadata");
    return XDP_DROP;
  }
  void* data = (void*)(long)ctx->data;
  void* data_end = (void*)(long)ctx->data_end;
  if((data + sizeof(struct UDPPacket)) >= data_end) {
    bpf_printk("Dropping small packet");
    u64 sz = data_end - data;
    return XDP_PASS;
  } else {

    struct UDPPacket* p = data;
    struct TCPPacket* oe = data;

    if(p->eth.h_proto != htons(ETH_P_IP)) {
	    bpf_printk("Forwarding Non IP %d", p->eth.h_proto);
      return XDP_PASS;
    }

    if(p->ip.protocol == 17 && p->packetType == 1 && p->udp.dest == htons(MD_UNICAST_PORT)) {
	    bpf_printk("Forwarding Non IP %d", p->eth.h_proto);
      return bpf_redirect_map(&redirMap, 0, XDP_PASS);
    } else if(p->udp.dest == htons(MD_UNICAST_PORT)) {
      bpf_printk("Malformed udp packet to md unicast port");
      return XDP_PASS;
    }

    if((data + sizeof(struct TCPPacket)) <= data_end && p->ip.protocol == 6 && oe->tcp.source == htons(OE_PORT)) {
	  bpf_printk("Forwarding oe ip proto: %d, source: %d", p->ip.protocol, htons(oe->tcp.source));
      return bpf_redirect_map(&redirMap, 0, XDP_PASS);
    }

	bpf_printk("Forwarding unclassified ip proto: %d, source: %d", p->ip.protocol, htons(oe->tcp.source));
    return XDP_PASS;
  }
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
