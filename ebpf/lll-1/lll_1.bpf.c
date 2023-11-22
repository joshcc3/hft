// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* This XDP program is only needed for multi-buffer and XDP_SHARED_UMEM modes.
 * If you do not use these modes, libbpf can supply an XDP program for you.
 */

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} xsks_map SEC(".maps");


int num_socks = 1;
int counter = 0;


SEC("xdp_sock")
int lll_1(struct xdp_md *ctx)
{
  bpf_printk("Packet %d", counter++);
  return bpf_redirect_map(&xsks_map, 0, XDP_DROP);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
