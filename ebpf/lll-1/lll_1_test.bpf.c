// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>

/* This XDP program is only needed for multi-buffer and XDP_SHARED_UMEM modes.
 * If you do not use these modes, libbpf can supply an XDP program for you.
 */

BPF_XSKMAP(xsks_map, 1);

int counter = 0;

int lll_1_test(struct xdp_md *ctx)
{
  bpf_trace_printk("Packet ");
  return XDP_PASS; // bpf_redirect_map(&xsks_map, 0, XDP_DROP);
}

