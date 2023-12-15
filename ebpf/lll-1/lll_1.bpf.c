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
    u8 packetType;
    char _padding[5];
} __attribute__ ((packed));

int num_socks = 1;
int counter = 0;


SEC("xdp")
int lll_1(struct xdp_md *ctx)
{


  if(ctx->data != ctx->data_meta) {
    bpf_printk("Unexpected metadata");
    return XDP_DROP;
  }
  void* data = (void*)(long)ctx->data;
  void* data_end = (void*)(long)ctx->data_end;

  if(data + 14 < data_end) {
    bpf_printk("%d: %x %x", counter++, ((u8*)data)[0], ((u8*)data)[1]);
    bpf_printk("%x %x %x", ((u8*)data)[2], ((u8*)data)[3], ((u8*)data)[4]);
    bpf_printk("%x %x %x", ((u8*)data)[5], ((u8*)data)[6], ((u8*)data)[7]);
    bpf_printk("%x %x %x", ((u8*)data)[8], ((u8*)data)[9], ((u8*)data)[11]);
    bpf_printk("%x %x %x", ((u8*)data)[12], ((u8*)data)[13], ((u8*)data)[14]);        
  }


  if((data + sizeof(struct Packet)) > data_end) {
    u64 sz = data_end - data;
    bpf_printk("small packet: %d", sz);
    return XDP_PASS;
  } else if((data + sizeof(struct Packet)) < data_end) {
    u64 sz = data_end - data;
    bpf_printk("large packet: %d", sz);
    return XDP_PASS;
  } else {

    struct Packet* p = data;
    if(p->eth.h_proto != htons(ETH_P_IP) || p->ip.protocol != 17) {
      bpf_printk("Passing non-udp packet");
      return XDP_PASS;
    }

    bpf_printk("Packet: %d, type %d", counter++, p->packetType);
    if(p->packetType == 2) {
      return bpf_redirect_map(&xsks_map, 0, XDP_DROP);
    }
    if(p->packetType == 1) {
      return bpf_redirect_map(&xsks_map, 0, XDP_DROP);
    }

    return XDP_PASS;
  }
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
