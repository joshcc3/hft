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
#define ntohs(x) ((__be16)___constant_swab16((x)))
#define htonl(x) ((__be32)___constant_swab32((x)))
#define INADDR_B(a, b, c, d) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))


struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} mdRedirMap SEC(".maps");


typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

//struct order_data {
//    int seqId;
//    int price;
//    char side;
//    int seqIdOut;
//} __attribute__ ((packed));

struct Packet {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
    u64 internal_sequence;
    u32 length;
    u16 type;
//    char _padding[5];
//    struct order_data od;
} __attribute__ ((packed));

int num_socks = 1;
int counter = 0;


SEC("xdp")
int lll_1(struct xdp_md *ctx)
{
//
//  if(ctx->data != ctx->data_meta) {
//    bpf_printk("Unexpected metadata");
//    return XDP_DROP;
//  }
  void* data = (void*)(long)ctx->data;
  void* data_end = (void*)(long)ctx->data_end;
  if((data + sizeof(struct Packet)) > data_end) {
    u64 sz = data_end - data;
    bpf_printk("small packet: %d", sz);
    return XDP_PASS;
  } else {

    struct Packet* p = data;
    if(p->eth.h_proto != htons(ETH_P_IP) || p->ip.protocol != 17) {
      bpf_printk("Passing non-udp packet");
      return XDP_PASS;
    }

      /*if (p->ip.daddr != INADDR_B(10, 50, 15, 47)) {
          bpf_printk("Host: %d, host ip %x", counter++, p->ip.daddr);
          return XDP_PASS;
      }*/

      // Check destination port
      u16 dest_port = ntohs(p->udp.dest);
      if (dest_port < 10000 || dest_port > 10300) {
          bpf_printk("Counter: %d, port %d, type %d", counter++, dest_port, p->type);
          return bpf_redirect_map(&mdRedirMap, 0, XDP_PASS);
      }

    bpf_printk("Packet: %d, type %d", counter++, p->type);

    return XDP_PASS;
  }
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
