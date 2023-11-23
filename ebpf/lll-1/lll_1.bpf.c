// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_xdp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/limits.h>
#include <linux/udp.h>

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
} xsks_map SEC(".maps");


typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

struct Packet {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
    u8 appPacketType;
} __attribute__ ((packed)) ;

int num_socks = 1;
int counter = 0;


SEC("xdp_sock")
int lll_1(struct xdp_md *ctx)
{
  bpf_printk("Packet %d", counter++);

  if(ctx->data != ctx->data_meta) {
    bpf_printk("Unexpected metadata");
    return XDP_DROP;
  }
  void* data = (void*)(long)ctx->data;
  void* data_end = (void*)(long)ctx->data_end;
  u64 sz = data_end - data;
  if(data + sizeof(struct Packet) > data_end) { //
    bpf_printk("Passing small packet");
    return XDP_PASS;
  } else {

    struct Packet* p = data;
    if(p->eth.h_proto != htons(ETH_P_IP) || p->ip.protocol != 17) {
      bpf_printk("Passing non-udp packet");
      return XDP_PASS;    
    }
    
    if(p->appPacketType != 1) {
      bpf_printk("Passing non market data packet");
      return XDP_PASS;    
    }

    bpf_printk("Forwarding marketdata packet");    
    return bpf_redirect_map(&xsks_map, 0, XDP_DROP);

  }
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
