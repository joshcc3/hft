python bpf reference:
https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md#13-bpf_xskmap

sudo bpftrace -e 'tracepoint:net:netif_receive_skb /str(args->name) == "lo"/ { printf("%s: %s\n", comm, str(args->name)); }'

/sys/kernel/debug/tracing/events/net/netif_receive_skb_entry/format

What is the needs_wakeup flag - i havent understood that.