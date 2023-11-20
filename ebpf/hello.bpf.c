#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int counter = 0;

SEC("xdp")
int hello(struct xdp_md *ctx) {
  counter++;

  bpf_printk("[%d] Msg", counter);
  if(ctx->data != ctx->data_meta) {
    bpf_printk("Meta data size (%d)", counter, (ctx->data_meta - ctx->data));
  }
  char* data_start = (char*)(long)ctx->data;
  char* data_end = (char*)(long)ctx->data_end;
  bpf_printk("Sz (%d)", data_end - data_start);

  int ethHeader = 14;
  int ipHeader = 20;

  if(data_start + ethHeader + ipHeader < data_end) {
    char typ0 = data_start[12];
    char typ1 = data_start[13];
    if(typ0 != 0x8 || typ1 != 0) {
      bpf_printk("Not ipv4[%x %x]", typ0, typ1);
      return XDP_PASS;
    }

    __u8 versionIHL = data_start[ethHeader];
    __u8 tos = data_start[ethHeader + 1];
    __u16 totalLenN = *(__u16*)(data_start + ethHeader + 2);
    __u16 totalLen = (totalLen << 8) | (totalLen >> 8);
    __u16 identifier = *(__u16*)(data_start + ethHeader + 4);
    __u16 flagFragOffs = *(__u16*)(data_start + ethHeader + 6);
    __u8 ttl = data_start[ethHeader + 8];
    __u8 protocol = data_start[ethHeader + 9];
    __u8* sourceAddr = (__u8*)(data_start + ethHeader + 12);
    __u32 destAddr = *(__u32*)(data_start + ethHeader + 16);    

    
    bpf_printk("ID (%d), TotalLen: (%u), TTL (%u)", identifier, totalLenN, ttl);
    bpf_printk("version (%d), ihl (%d), tos (%d)",
	       (versionIHL >> 4),
	       (versionIHL & 0xf),
	       tos);
    bpf_printk("Protocol (%d)", protocol);
    bpf_printk("Source: %d %d %d", sourceAddr[0], sourceAddr[1], sourceAddr[2]);
    bpf_printk("Source: %d", sourceAddr[3]);        
    
    
    return XDP_PASS;
  } else {
    bpf_printk("Unexpected Small Packet (%d)", data_end - data_start);
    return XDP_PASS;
  }
  

}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
