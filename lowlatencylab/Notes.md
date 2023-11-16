Path through a network socket


https://www.privateinternetaccess.com/blog/linux-networking-stack-from-the-ground-up-part-4-2/

Driver is loaded and initialized.
Packet arrives at the NIC from the network.
Packet is copied (via DMA) to a ring buffer in kernel memory.
Hardware interrupt is generated to let the system know a packet is in memory.
Driver calls into NAPI to start a poll loop if one was not running already.
ksoftirqd processes run on each CPU on the system. They are registered at boot time. The ksoftirqd processes pull packets off the ring buffer by calling the NAPI poll function that the device driver registered during initialization.
Memory regions in the ring buffer that have had network data written to them are unmapped.
Data that was DMA’d into memory is passed up the networking layer as an ‘skb’ for more processing.
Packet steering happens to distribute packet processing load to multiple CPUs (in leu of a NIC with multiple receive queues), if enabled.
Packets are handed to the protocol layers from the queues.
Protocol layers add them to receive buffers attached to sockets.


CLoudflare blogs:
https://blog.cloudflare.com/how-to-achieve-low-latency/

https://blog.cloudflare.com/virtual-networking-101-understanding-tap/
https://blog.cloudflare.com/a-story-about-af-xdp-network-namespaces-and-a-cookie/


BPF:
https://www.brendangregg.com/blog/2019-12-02/bpf-a-new-type-of-software.html
Brendan gregg blog: https://www.brendangregg.com/blog/index.html


XDP articles:
https://lwn.net/Articles/750845/
https://docs.cilium.io/en/latest/bpf/resources/

http://vger.kernel.org/lpc_net2018_talks/lpc18_paper_af_xdp_perf-v2.pdf
https://sematext.com/blog/ebpf-and-xdp-for-processing-packets-at-bare-metal-speed/
https://www.spinics.net/lists/xdp-newbies/msg00185.html

On writing a tcp/ip stack:
https://williamdurand.fr/2022/04/11/on-writing-a-network-stack-part-2/
https://www.saminiir.com/lets-code-tcp-ip-stack-1-ethernet-arp/

Articles on kernel networking:
http://vger.kernel.org/~davem/skb_data.html
https://man7.org/linux/man-pages/man7/netdevice.7.html
https://www.cs.dartmouth.edu/~sergey/netreads/path-of-packet/Network_stack.pdf

datasheets for eth controllers:
file:///home/jc/Downloads/E810_Datasheet_Rev2.6.pdf
https://network.nvidia.com/pdf/applications/SB_HighFreq_Trading.pdf
https://courses.cs.washington.edu/courses/cse451/16au/readings/e1000e.pdf
https://www.intel.com/content/dam/doc/datasheet/82571eb-82572ei-gbe-controller-datasheet.pdf


Qemu deep dives:
https://airbus-seclab.github.io/qemu_blog/pci.html

We are going to use raw_sockets to see how fast we can now process them instead.

[jc@fedora hft]$ clang++ -O3 -o lll_strategy -luring lowlatencylab/client/mdmcclient.cpp lowlatencylab/client/L2OB.cpp&& ./lll_strategy


Perf optimization notes:
As it stands (5aeb0049753b319177fb83ab61d1da2fd1d85baf) - the strategy has the following breakdown :
Iters [10240]
Prev Avg Loop Time [22.8359us]
Prev Time Spend [22.649us]
Total Packet Proc [24.4487us]
Book update [4.91729%]
Order Submission [0.438054%]
Message Handling [7.60806%]
Recv [89.3757%]


It goes through 10240 iterations, The loop time of receiving a marketdata packet to responding on average currently stands at 22us.
Prev avg loop time is the time it takes to go through a loop of checking md, updating ob, sending orders checking completions and then back.
The recv is the bottleneck over here.

----
Understanding bpf: https://docs.cilium.io/en/latest/bpf/architecture/

