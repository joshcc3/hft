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


----



In high-frequency trading (HFT), performance is critical, and specific C++ libraries are favored for their efficiency. Here are examples focusing on containers, shared memory access, and networking:

Containers:

Boost.Container: Offers STL-compatible containers optimized for performance.
Abseil: Google's open-source collection that includes high-performance data structures.
Shared Memory Access:

Boost.Interprocess: Facilitates shared memory communication, often used for low-latency inter-process communication in HFT systems.
Intel TBB (Threading Building Blocks): Provides concurrent data structures and memory allocation optimization.
Networking:

ZeroMQ: A high-performance asynchronous messaging library, suitable for scalable distributed or concurrent applications.
Asio (standalone or part of Boost): A cross-platform C++ library for network and low-level I/O programming, offering a consistent asynchronous model.
These libraries are valued in HFT for their ability to minimize latency and maximize throughput, which are crucial in a field where milliseconds can make a significant difference.




User
give me 10 more
ChatGPT
Here are 10 additional high-performance C++ libraries often used in high-frequency trading (HFT), focusing on containers, shared memory access, and networking:

folly (Facebook Open-source Library): Offers a variety of core libraries, including performant data structures.

FlatBuffers (Google): Efficient cross-platform serialization library, useful for network communication.

Cap'n Proto: A fast data interchange format and capability-based RPC system.

nanomsg: A networking library for building scalable and fast distributed systems.

Hazelcast C++ Client: In-memory data grid for distributed caching and computation.

SPSCQueue: A single-producer, single-consumer lock-free queue for high-performance scenarios.

TBB's concurrent_hash_map (Intel): Part of Threading Building Blocks, optimized for concurrent access.

fastflow: A C++ parallel programming framework for shared memory.

librdkafka: A high-performance C library implementation of the Apache Kafka protocol for distributed messaging.

Seastar: An advanced, open-source C++ framework for high-performance server applications on modern hardware.

These libraries are designed to maximize efficiency and speed, making them suitable for the demands of HFT environments.


