Notes on the linux kernel

tracepoints in /sys/kernel/debug/tracing/events
sudo bpftrace -e 'tracepoint:net:netif_receive_skb /str(args->name) == "lo"/ { printf("%s: %s\n", comm, str(args->name)); }'
root@fedora:/home/joshuacoutinho/CLionProjects/hft/lowlatencylab/client/bpf# sudo bpftrace -e 'tracepoint:net:netif_receive_skb /str(args->name) == "lo" && comm=="strategy"/ { printf("%s: %d\n", comm, ((struct sk_buff*)(args->skbaddr))->pkt_type); }'


The counter updated by __IP_UPD_PO_STATS in the Linux kernel can typically be checked in the /proc/net/snmp file. This file contains various network-related statistics for different protocols, including IP.
When you inspect the /proc/net/snmp file, you'll find lines starting with "Ip:" followed by a series of numbers. These numbers represent different IP-related statistics as defined by MIB (Management Information Base) from the SNMP (Simple Network Management Protocol) standards. The specific counter incremented by __IP_UPD_PO_STATS (which depends on what exact statistic it's updating) will be one of these numbers.
To view the file, you can use the following command in a terminal:
cat /proc/net/snmp


network code in net/core/dev.c
    
netif_receive_skb
netif_receive_skb_internal
__netif_receive_skb_one_core
__netif_receive_skb_core
deliver_skb
ip_recv
ip_recv_core


simple hist example
    # echo 'hist:keys=skbaddr.hex:vals=len' > \
      /sys/kernel/tracing/events/net/netif_rx/trigger

    # cat /sys/kernel/tracing/events/net/netif_rx/hist

    # echo '!hist:keys=skbaddr.hex:vals=len' > \
      /sys/kernel/tracing/events/net/netif_rx/trigger

simple conditional trigger example
echo 'hist:keys=skbaddr.hex:vals=len:pause' > /sys/kernel/tracing/events/net/netif_receive_skb/trigger
echo 'enable_hist:net:netif_receive_skb if filename==/usr/bin/wget' > /sys/kernel/tracing/events/sched/sched_process_exec/trigger
echo 'disable_hist:net:netif_receive_skb if comm==wget' > /sys/kernel/tracing/events/sched/sched_process_exit/trigger

to get stack traces:
echo 'hist:keys=common_stacktrace:values=bytes_req,bytes_alloc:sort=bytes_alloc' > /sys/kernel/tracing/events/kmem/kmalloc/trigger

to get net if receive stack traces
echo 'hist:keys=common_stacktrace:vals=len:pause' > /sys/kernel/tracing/events/net/netif_receive_skb/trigger
echo 'enable_hist:net:netif_receive_skb if filename=="./strategy"' > /sys/kernel/tracing/events/sched/sched_process_exec/trigger
echo 'disable_hist:net:netif_receive_skb if comm==strategy' > /sys/kernel/tracing/events/sched/sched_process_exit/trigger
cat /sys/kernel/tracing/events/net/netif_receive_skb/hist| less

This is the kernel stacktrace w.r.t. sending data for marketdat and sending data on the xsk socket:
{ common_stacktrace:
__netif_receive_skb_one_core+0x3c/0xa0
process_backlog+0x85/0x120
__napi_poll+0x28/0x1b0
net_rx_action+0x2a4/0x380
__do_softirq+0xd1/0x2c8
do_softirq.part.0+0x3d/0x60
__local_bh_enable_ip+0x68/0x70
__dev_direct_xmit+0x152/0x210
__xsk_generic_xmit+0x3e4/0x710
xsk_sendmsg+0x12f/0x1f0
__sys_sendto+0x1d6/0x1e0
__x64_sys_sendto+0x24/0x30
do_syscall_64+0x5d/0x90
entry_SYSCALL_64_after_hwframe+0x6e/0xd8
} hitcount:         40  len:       3160
{ common_stacktrace:
__netif_receive_skb_one_core+0x3c/0xa0
process_backlog+0x85/0x120
__napi_poll+0x28/0x1b0
net_rx_action+0x2a4/0x380
__do_softirq+0xd1/0x2c8
do_softirq.part.0+0x3d/0x60
__local_bh_enable_ip+0x68/0x70
__dev_queue_xmit+0x28b/0xd90
ip_finish_output2+0x2a1/0x570
ip_send_skb+0x86/0x90
udp_send_skb+0x153/0x350
udp_sendmsg+0xb7e/0xf20
__sys_sendto+0x1d0/0x1e0
__x64_sys_sendto+0x24/0x30
do_syscall_64+0x5d/0x90
entry_SYSCALL_64_after_hwframe+0x6e/0xd8
} hitcount:         48  len:      10784



udo bpftrace -e 'kprobe:ip_rcv /comm=="nc"/ { printf("%s\n", ((struct net_device*)arg1)->name); }'


which events:
whats the condition
how to do stack trace and rank them


root@fedora:/home/joshuacoutinho/CLionProjects/hft/lowlatencylab/client/bpf# sudo vim /etc/firewalld/firewalld.conf 


ipt_do_table - not called
ipv4_conntrack_defrag - not called
ipv4_conntract_in - not called
nf_nat_ipv4_pre_routing - not called

ip_rcv_finish - not called

use cat /proc/kallsyms to get the name of a function you know the address to.

So there is an issue with the loopback address