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


https://blog.cloudflare.com/virtual-networking-101-understanding-tap/


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
