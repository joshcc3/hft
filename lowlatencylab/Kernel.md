Notes on the linux kernel

Extensions: Hack the pcie infrastructure of the kernel itself.
Fun idea: use the gpio pins to generate an interrupt when the nic detects a the ethernet preamble.

Good book on linux kernel
https://0xax.gitbooks.io/linux-insides/content/SyncPrim/linux-sync-2.html

Useful to know:

The value of VMALLOCBASE varies depending on whether you're using a 32-bit x86 system or a 64-bit x86_64 system. In general, VMALLOCBASE is the starting address of the vmalloc area in the kernel's virtual memory layout.

For 32-bit x86 systems, VMALLOCBASE is typically set to 0xC0000000.
For 64-bit x86_64 systems, VMALLOCBASE is typically set to 0xffffc20000000000.

#define __START_KERNEL_map	_AC(0xffffffff80000000, UL)
kernel data is in high memory and is mapped directly to physical pages starting at offset 0.
virtual memory is mapped relative to PAGE_OFFSET.


a q_vector is the target for MSIX interrupts. You configure the hardware interrupt table using mmio writes.
each location corresponds to a specific q_vector. igb_assign_vector is relevant here
write_ivar:
 *  This function is intended to handle the writing of the IVAR register                                              
 *  for adapters 82576 and newer.  The IVAR table consists of 2 columns,                                              
 *  each containing an cause allocation for an Rx and Tx ring, and a                                                  
 *  variable number of rows depending on the number of queues supported.

There is a qvector per read and per tx queue

https://docs.kernel.org/core-api/dma-api-howto.html
sudo cat /proc/iomem gives a layout of the physical memory and device mappings
it shows which buses correspond to which addresses.


You start by configuring the msix to point to a specific qvector. you associate the qvector with an interrupt
using the kernel function request_irq to which you give the qvector, a name and callback function. this is featured
in the /proc/interrupts file.
On an interrupt occurring, the callback igb_msix_ring is invoked. this schedules napi and goes to sleep.
then igb_poll is called to service the tx or rx interrupt.


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

ip netns exec ns2 /home/joshuacoutinho/CLionProjects/hft/cmake-build-debug/lll_exchange

ip netns exec ns1 bash -c  "bpftool net detach xdpdrv dev veth1 && bpftool prog load /home/joshuacoutinho/CLionProjects/hft/lowlatencylab/client/bpf/strat.bpf.o /sys/fs/bpf/strat && bpftool net attach xdpdrv name strat dev veth1 && /home/joshuacoutinho/CLionProjects/hft/cmake-build-debug/lll_strategy

Nice guide on kernel debugging:
https://blog.cloudflare.com/a-story-about-af-xdp-network-namespaces-and-a-cookie/



https://git.esa.informatik.tu-darmstadt.de/net/100-gbps-fpga-tcp-ip-stack


notes on device drivers
https://www.privateinternetaccess.com/blog/linux-networking-stack-from-the-ground-up-part-2/

https://lwn.net/Articles/914992/

KERNEL_VERSION=6.7.0-rc3-00288-g441546c15745-dirty;ROOTFS=/home/joshuacoutinho/CLionProjects/qemu-8.1.3/isos/igb-patch.qcow2; sudo qemu -nbd --connect=/dev/nbd1 $ROOTFS  && sudo mount /dev/nbd1p1 /mnt/igb-vm/ && sudo cp -r /lib/modules/$KERNEL_VERSION/ /mnt/igb-vm/lib/modules/ && sudo umount /mnt/igb-vm  && sudo qemu-nbd --disconnect /dev/nbd1

sudo modprobe nbd max_part=8
KERNEL_VERSION=6.7.0-rc3-00292-gf878ded19be6-dirty;ROOTFS=/home/joshuacoutinho/CLionProjects/qemu-8.1.3/isos/igb-patch.qcow2; make -j12 && sudo make modules_install && sudo make install && sudo qemu-nbd --connect=/dev/nbd1 $ROOTFS  && sudo mount /dev/nbd1p1 /mnt/igb-vm/ && sudo cp -r /lib/modules/$KERNEL_VERSION/ /mnt/igb-vm/lib/modules/ && sudo umount /mnt/igb-vm  && sudo qemu-nbd --disconnect /dev/nbd1 && gdb -ex "target remote :1234" -ex "c" vmlinux


How to Determine the Exact Location of the Crash
Check the Source Code: If you have access to the source code of the igb module, open the josh_handle_packets function and count 0x27a bytes from the start of the function. This should bring you to the approximate location of the crash.

Use GDB: If you have a compiled version of the igb module with debugging symbols (igb.ko with debug info), you can use GDB to find the exact line of the crash:

bash
Copy code
gdb /path/to/igb.ko
(gdb) list *(josh_handle_packets+0x27a)
Replace /path/to/igb.ko with the actual path to your compiled igb module.

Reproduce and Debug: If you can reproduce the issue, running the module under a debugger or analyzing core dumps (if available) can provide more context.

Kernel Debugging Tools: Using tools like kdump and crash to analyze kernel crash dumps can also be very helpful in complex scenarios.

Understanding kernel panics and debugging them effectively often requires a deep understanding of both the Linux kernel and the specific drivers or modules involved. If you're not familiar with kernel development, consider seeking help from someone who is, or from the community around the igb module or driver.
----


c++ libraries: concurr and mthreading

[root@fedora ~]# ip addr add 192.168.100.2/24 dev enp0s3 && nc -l 192.168.100.2 1234 > /lib/modules/6.7.0-rc3-00287-g7e7fb23f73d1-dirty/kernel/drivers/net/ethernet/josh.ko 

https://blog.packagecloud.io/monitoring-tuning-linux-networking-stack-receiving-data/

For gdb debugging in the kernel
https://nickdesaulniers.github.io/blog/2018/10/24/booting-a-custom-linux-kernel-in-qemu-and-debugging-it-with-gdb/
In addition you have to run make menuconfig and set
You can easily compile the kernel with debug symbols by enabling CONFIG_DEBUG_INFO . If you are using make menuconfig it's going to be under "Kernel hacking" -> "Compile-time checks and compiler options" -> "Compiler a kernel with debug info".
In order to get the symbols loaded for a dynamically loaded module, use lx-symbols. This will load the symbols for the module igb, you can then set a hardware breakpoint at the corresponding location.


sudo qemu-system-x86_64 -s -S    -m 2048 -cpu host --enable-kvm -kernel /boot/vmlinuz-6.7.0-rc3-00292-gf878ded19be6-dirty -initrd /boot/initramfs-6.7.0-rc3-00292-gf878ded19be6-dirty.img -hda CLionProjects/qemu-8.1.3/isos/igb-patch.qcow2  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device igb,netdev=net0,id=nic0  -nographic -append "root=/dev/sda1 rw nokaslr console=ttyS0 init=/lib/systemd/systemd"


git submodule add https://github.com/username/submodule-repo.git external/submodule-repo
git submodule init
git submodule update






-----
I have some code that blocks on a recv and once that completes proceeds to parse the packet and performs some work. then in a loop blocks on a recv and then does some more work. I'm replacing the current library for recv with something that is interrupt driven. i.e. packet arrival is announced via an interrupt and my interrupt handler is called. The application is low latency and I want to handle the reception of the packets in the flow as when the interrupt handler is called. My question is what's the best way to refactor the existing code without too many changes.
One idea I had was to use continuation passing style. The io module has a continuation that represents what to do on packet received. That continuation would represent the current code all the way from waking up from the blocking recv to blocking the next receive. The blocking recv is called in 2 functions that are both called from 3 different places that both are called from a number of places.
I have a couple of questions about how to achieve and code this.
No. 1, how do I represent the continuation?
- as a function object? but how do I then capture the fact that I must start from the point just after the blocking recv with the context of what called this function and where to return to.
- I think the c++ coroutines api is the correct way to do this however I do not have access to the c++ stdlib so I shall have to implement it myself

The problem is that alot of the recv calls are in function calls that are called from many different places. 


sudo sysctl -w net.ipv6.conf.eth0.disable_ipv6=1


----

why does it crash when we set our processed field?
open questions: why does it not accept packets before we have assigned an ip address?
why does it keep losing its ip address?
