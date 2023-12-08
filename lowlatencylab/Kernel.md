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
echo 'enable_hist:net:netif_receive_skb if filename=="./tcptest"' > /sys/kernel/tracing/events/sched/sched_process_exec/trigger
echo 'disable_hist:net:netif_receive_skb if comm=="tcptest"' > /sys/kernel/tracing/events/sched/sched_process_exit/trigger
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

To construct a veth pair that can communicate across namespaces.

ip link add veth1 type veth peer name veth2
ip link set dev veth1 address 00:00:00:00:00:01
ip link set dev veth2 address 00:00:00:00:00:02
ip netns add ns1
ip netns add ns2
ip link set veth1 netns ns1
ip link set veth2 netns ns2
ip netns exec ns1 ip addr add 10.0.0.1/24 dev veth1
ip netns exec ns2 ip addr add 10.0.0.2/24 dev veth2
ip netns exec ns1 sysctl -w net.ipv6.conf.veth1.disable_ipv6=1
ip netns exec ns2 sysctl -w net.ipv6.conf.veth2.disable_ipv6=1
ip netns exec ns1 ip link set veth1 up
ip netns exec ns2 ip link set veth2 up
ip netns exec ns2 nc -lu 10.0.0.2 4096 -v
ip netns exec ns1 ./xdpsock -t -i veth1 -S -c -G 00:00:00:00:00:02 -H 00:00:00:00:00:01


When running a bpf program in a namespace - pinning doesn't work because ip netns exec mounts and unmounts bpf every time its called.
remember to load and attach your program from within the namespace and call trace log from within the namespace as well.

netns exec ns1 bash -c  "bpftool prog pin name strat /home/joshuacoutinho/CLionProjects/hft/lowlatencylab/client/bpf/strat.bpf.o /sys/fs/bpf/strat && /home/joshuacoutinho/CLionProjects/hft/cmake-build-debug/lll_strategy 
netns exec ns1 bash -c  "bpftool net detach xdpdrv dev veth1 && bpftool prog load /home/joshuacoutinho/CLionProjects/hft/lowlatencylab/client/bpf/strat.bpf.o /sys/fs/bpf/strat && bpftool net attach xdpdrv name strat dev veth1 && /home/joshuacoutinho/CLionProjects/hft/cmake-build-debug/lll_strategy
netns exec ns1 bash -c  "bpftool net detach xdpdrv dev veth1 && bpftool prog load /home/joshuacoutinho/CLionProjects/hft/lowlatencylab/client/bpf/strat.bpf.o /sys/fs/bpf/strat && bpftool net attach xdpdrv name strat dev veth1 && /home/joshuacoutinho/CLionProjects/hft/cmake-build-debug/lll_strategy

netns exec ns2 /home/joshuacoutinho/CLionProjects/hft/cmake-build-debug/lll_exchange

Nice guide on kernel debugging:
https://blog.cloudflare.com/a-story-about-af-xdp-network-namespaces-and-a-cookie/



https://git.esa.informatik.tu-darmstadt.de/net/100-gbps-fpga-tcp-ip-stack


notes on device drivers
https://www.privateinternetaccess.com/blog/linux-networking-stack-from-the-ground-up-part-2/