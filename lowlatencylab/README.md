Mkonjibhu123!

Goal is to setup a testbench to test optimizations to sending and receiving packets through the linux kernel.
Problem statement:
I want to set up the following lab using qemu.
We have a 1MB of udp packets that represent marketdata updates published on a multicast address.
We also have a tcp connection for orderentry established to a server.
The moment we see a marketdata packet corresponding to a top of book update whose size > $1000000 we would like to agress
on the other side and send a tcp packet with an order to agress on the other side.
The goal is to minimize the latency between detecting the good packet to shooting an order.
In order to setup this lab and try different experiments on the network device and the kernel,
I want a tap device which I'll send the marketdata frames to and receive the tcp orders.
I want to use qemu to emulate the network card and the pcie infra and everything between that and my program that reads
the marketdata and responds with order requests.

The structure of the exchange:
I would like you to implement the following spec for an exchange server:
There are 3 classes, an order entry server, a marketdata server and an exchange. 
The exchange moves through 3 states - INIT, HANDSHAKE, TRANSMIT. 
Order Entry Server is a tcp server that listens for connections. 
A client will send it only 1 type of message. That message is a fixed binary format 
which consists of 4 bytes for quantity, 8 bytes of price and 1 byte for side.  
It can only receive one connection at a time when the exchange is in the init state. 
The exchange then moves to the handshake state. The order entry server cannot receive orders
until the exchange is in the transmit stage.  Once the server has an open connection and it's 
connected to the multicast address. It reads data from a gzip marketdata file. 
It keeps reading the lines and publishing them to the multicast address as long as the line 
contains the field, snapshot = true. Have a function stub for me to fill in
that takes the string line and formats a buffer for the output packet. 
Once the snapshot lines are done (detected by seeing a message with snapshot false) 
the exchange moves to the transmit stage. It continues reading the marketdata from a gzip file 
line by line formats the line into a packet and then sends the multicast packet down the network. 
It is simultaneously waiting for messages on the order entry path tcp path. 
When it receives a message on the order entry path it logs the fields and the time it received it to stdout. 
If the order entry path disconnects the exchange stops sending multicast data, closes all active connections and files
however keeps the servers up and listening for connections and moves back to the init state.
The trigger id is the id of the marketdata.
We setup the server to only send non-blocking.

Notes on io_uring:
use the params in queue_init to check for features.
there are many features around polling that might be more suitable for my workload.



We want to implement a trading strategy. Write c++ code that does the following. It should use the io_uring library
for all networking and io operations.
It first connects to localhost port 9012 for order entry.
After the connection is established it listens to the multicast address 239.255.0.1:12345
for l2 marketdata packets. The packets are ascii with the fields 'exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount'.
We shall treat the local_timestamp as the sequence number.
Every packet it sees, it parses (just call out to a stub that gets the individual fields) and then
updates an l2 lob. the messages will either be update or cancel. pass the is_snapshot field to the update. 
Cancels can never be snapshot. A message is a cancel if the qty is 0.
If the update is within 5 levels of the top and the size increase to the level is > 50000000, then we want to send an
aggressive limit to the top level of the book on the other side.



Use the packet system call to get packets directly from the link layer.
use PACKET_QDISC_BYPASS
MSG_ZEROCOPY for sending packets
use packet_rx_ring - tools/testing/selftests/net/psock_tpacket.c
look at AF_XDP instead  - see if you can use natve or offloaded xdp
https://lwn.net/Articles/750845/
https://www.kernel.org/doc/html/v5.3/networking/af_xdp.html

https://www.spinics.net/lists/xdp-newbies/msg00185.html



look at huge pages and tlb misses.


IORING_SETUP_SQPOLL in io_uring_setup


IORING_OP_SEND_ZC iouring flags

IP_TOS (since Linux 1.0)

Use POLLHUP - to determine when the socket has hung up



Use SIOCGSTAMP - for round trip time measurements.


For io_uring:
https://kernel.dk/io_uring.pdf

https://javadoc.lwjgl.org/org/lwjgl/system/linux/liburing/LibIOURing.html

iouring examples:
https://github.com/axboe/liburing/blob/master/examples/io_uring-udp.c


Debugging why:
So back to the issue of the receiver not getting multicast data:
I observe that when I use imr_interface.s_addr to INADDR_ANY like so:
ip_mreq mreq{};
mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR.c_str());
mreq.imr_interface.s_addr = htonl(INADDR_ANY);
if (setsockopt(mdFD, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
perror("Membership join");
close(mdFD);
exit(EXIT_FAILURE);
}


I start receiving multicast data.
However, if I switch that to any of the ip addresses from here:
1: lo: <LOOPBACK,MULTICAST,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
inet 127.0.0.1/8 scope host lo
valid_lft forever preferred_lft forever
inet6 ::1/128 scope host
valid_lft forever preferred_lft forever
2: wlp0s20f3: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP group default qlen 1000
link/ether 14:75:5b:60:2c:05 brd ff:ff:ff:ff:ff:ff
inet 192.168.0.106/24 brd 192.168.0.255 scope global dynamic noprefixroute wlp0s20f3
valid_lft 4553sec preferred_lft 4553sec
inet6 fe80::36e6:fcf0:9163:7640/64 scope link noprefixroute
valid_lft forever preferred_lft forever
3: tap0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc fq_codel state DOWN group default qlen 1000
link/ether 02:75:81:51:a8:9f brd ff:ff:ff:ff:ff:ff
inet 192.168.100.1/24 scope global tap0
valid_lft forever preferred_lft forever
inet6 fe80::75:81ff:fe51:a89f/64 scope link
valid_lft forever preferred_lft forever

I get no multicast data from any of them. Do you know why I'm not getting multicast data from any of the interfaces but I do get when I set the interface to INADDR_ANY? 

When you specify INADDR_ANY in the ip_mreq structure for multicast membership, you're telling the operating system to choose the default interface for multicast traffic. This is often what you want for a simple setup where the machine may have several interfaces but you want to listen for multicast packets on all of them or on the "default" one that the routing table indicates should be used for multicast.

The behavior you're observing where setting imr_interface.s_addr to an interface's IP address doesn't work while INADDR_ANY does might be due to a few reasons:

Routing Table Misconfiguration:
Multicast packets are routed based on the system's routing table. The routing table must have proper routes set up for multicast traffic. When using INADDR_ANY, the system's routing logic automatically picks the right interface based on the routing table and the source of the packets. When you specify an interface directly by setting an IP address, you bypass this logic, and if the routing table isn't set up correctly for this scenario, packets won't be received.

Interface Support:
Not all interfaces support multicast traffic equally. For example, the lo (loopback) interface can be used for multicast, but only for traffic originating from the same machine. In contrast, your wireless interface wlp0s20f3 might be the one that actually connects to the network where multicast packets are being distributed.

Firewall Rules:
Some systems have firewall rules that affect how multicast traffic is received. These rules might be set up to allow such traffic only on certain interfaces.

Scope of Multicast Address:
The multicast address used also has a scope. For example, the 239.255.0.1 is in the Organization-Local Scope (addresses from 239.255.0.0 to 239.255.255.255), which means it's meant to be used within the same organization or site, and routers generally do not forward these packets beyond their local subnets.

Application Binding to Specific IP:
If an application binds to a specific IP address (like one from your wlp0s20f3 interface), it will only receive multicast packets on that interface. In contrast, binding to INADDR_ANY will listen on all interfaces, and the kernel joins the multicast group on the default interface unless you tell it otherwise with additional socket options or routing table configurations.

To ensure that you receive multicast traffic on a specific interface when not using INADDR_ANY, you should:

Make sure your routing table is configured to route multicast traffic through that interface.
Confirm that there's no firewall blocking the traffic.
Use setsockopt with IP_MULTICAST_IF to set the outbound interface for multicast packets if you're sending data.
If your machine has multiple interfaces and you're joining a multicast group on an interface that isn't the default one for multicast, you need to ensure that you've set up your multicast routes accordingly.
Here's how to specify an interface with IP_MULTICAST_IF:


The routing table uses the default one, what's the default interface and why wouldn't it be one of the ones in ifconfig.
How can I get the routing table.
What are the different scopes for multicast addresses?
What is the default interface?

set IP_MULTICAST_IF

When you're dealing with multicast on a network socket in a Unix-like operating system, there are several aspects to consider to understand which interfaces are being used for sending and receiving multicast packets:

Default Interface
The default interface for multicast transmission is typically the system's "default" network interface, which is usually determined by the routing metric of the OS. This is not a multicast-specific setting but rather the interface that the operating system uses for outgoing traffic when no specific route is defined for a given destination.

Viewing the Routing Table
To see the routing table, you can use the netstat command with the -nr options (or route -n on some Unix-like systems):

sh
Copy code
netstat -nr
Or

sh
Copy code
route -n
This shows you the kernel routing tables. For multicast specific routes, you might not see anything specific because multicast routing is usually managed differently than unicast routing.

Multicast Routing
Linux systems use IGMP (Internet Group Management Protocol) to manage multicast membership, and you can check which multicast groups your system is a part of using the following command:

sh
Copy code
netstat -ng
Publishing on Interfaces
When you create a multicast sender with the provided code, you are sending data out to the multicast group specified by MCAST_ADDR with a TTL (Time To Live) of 10. The TTL value in a multicast packet determines the scope or range that the packet will be forwarded by routers. With a TTL of 10, the packet is allowed to be forwarded up to 10 router hops away.

The socket options you've set in the code (e.g., IP_MULTICAST_TTL and IP_MULTICAST_LOOP) affect how the multicast packets are sent:

IP_MULTICAST_LOOP set to 1 means that the multicast packets you send will also be received by the local machine's multicast listeners if any are active. This is useful for loopback testing.
IP_MULTICAST_TTL defines how many "hops" the multicast packet can take in the network. This does not bind the sending to any specific interface; rather, it's a limit on how far the packet can travel.
If you want to ensure that multicast packets are sent over a specific interface, you need to use the IP_MULTICAST_IF socket option. This allows you to specify an outgoing interface for multicast packets. Without setting IP_MULTICAST_IF, the system's routing table will determine the default outgoing interface.

Here's an example of setting IP_MULTICAST_IF:

cpp
Copy code
struct in_addr localInterface;
localInterface.s_addr = inet_addr("your_specific_interface_ip");
if (setsockopt(serverFD, IPPROTO_IP, IP_MULTICAST_IF, &localInterface,
sizeof(localInterface)) < 0) {
perror("setsockopt - IP_MULTICAST_IF");
close(serverFD);
exit(EXIT_FAILURE);
}
You would replace "your_specific_interface_ip" with the IP address of the interface you wish to use for transmitting multicast packets. This is typically the IP address of the interface that is connected to the network where you want the multicast packets to be distributed.



To send multicast data you publish to a multicast address.
To kernel sees that message and if you've considered loopback passes it back to the system.
You can create an explicit route in your routing tables for packets to that mcast group which should allow you to forward packets to a specific interface.
you add membership for a multicast group which creates a route in the kernel table for that.
then you bind to an interface to listen for mcast packets.
I think the kernel doesn't forward the packets on a loopback to any physical interfaces so only virtual devices see these packets or the broadcast address.
    


MD Pcaps: https://databento.com/pcaps#samples


sudo ip tuntap add dev tap0 mode tap user `whoami`
sudo ip link set tap0 up
sudo ip addr add 192.168.100.1/24 dev tap0

on the guest vm:
sudo ip addr add 192.168.100.2/24 dev ens3

nc -lk 1234 on guest
echo abcd | nc 192.168.100.2 1234 on host to test network connectivity.


yum install bridge-utils

./qemu-system-x86_64 -m 1024 -enable-kvm -drive if=virtio,file=test.qcow2,cache=none -cdrom ~/Downloads/Fedora-Workstation-Live-x86_64-38-1.6.iso \
-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
-device e1000e,netdev=net0,id=nic0
mkonjibhu123
user jc
edited sudoers and turned off the firewall from the management console here: 192.168.100.2:9090.


I tried getting multicast traffic looped back to the host forwarded to the tap device. The loopback works.
Adding a listener to ensure the multicast group is subscribed to and adding the following rules don't do anything to forward the traffic.
I suspect the tap0 is not getting the traffic or if it is qemu is not forwarding the traffic to the nic. either way something to investigate and experiment on.


A simple way to route specific packets that youre interested in:
mark the packets in pre routing
sudo iptables -t mangle -A PREROUTING -d 239.255.0.1 -p udp --dport 12345 -j MARK --set-mark 1

# Create a new routing table with a name, for example, "multicast"
echo "200 multicast" | sudo tee -a /etc/iproute2/rt_tables

# Add a routing rule that uses the "multicast" table for marked packets
sudo ip rule add fwmark 1 table multicast

# Set the default route for this table to go out via tap0
sudo ip route add default dev tap0 table multicast


{ - this doesn't work
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -A FORWARD -i wlp0s20f3 -o tap0 -d 239.255.0.1 -j ACCEPT
}

Details for packet info on a machine:
/proc/net/dev - live packet info
/proc/net/snmp
ip -s link
ss -s
iptables -L -v -n
nft list ruleset
tc -s qdisc show dev eth0
ethtool -S eth0

----------------------


------------------

debugging why packets are being dropped:
/proc/net/snmp is invaluable:

Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs InEr
rs OutRsts InCsumErrors
Tcp: 1 200 120000 -1 169558 83834 4467 9373 36 7750904 7437847 80969 969 42987 0
Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors InCsumErrors IgnoredMulti MemErrors
Udp: 7891464 10 61057700 120022858 61057700 3037386066 0 40171429 0


recvbuf and sndbug numbers as well as inerrors or ignoredmulti

I get recvbuferrors however the packets are being delievered in order.


----

[jc@fedora data]$ sudo tcpdump -i any -w multicast_dump10ms.pcap 'udp and port 12345 and dst 239.255.0.1'


https://blog.cloudflare.com/missing-manuals-io_uring-worker-pool/

netstat -su

/proc/net/udp
111675752 packet receive errors
174849548 packets sent
111675752 receive buffer errors

    111680564 packet receive errors
    174854709 packets sent
    111680564 receive buffer errors

ChatGPT
In the netstat -su command output for UDP statistics, the terms "packet receive errors" and "receive buffer errors" refer to specific issues encountered by the UDP protocol when processing incoming packets:

Packet Receive Errors:
This statistic represents the number of all errors that occurred during packet reception. This is a cumulative count of all issues that prevented a packet from being processed successfully, which can include:

Checksum errors, indicating that the packet data was corrupted in transit.
Packets that exceed the maximum transmission unit (MTU) and cannot be reassembled.
Packets that arrived at a closed port, with no listening process.
Packets that could not be processed for other reasons, like header issues.
Receive Buffer Errors:
These errors specifically count the number of packets that have been lost due to insufficient buffer space. When the UDP protocol receives a packet, it places it into a queue (socket receive buffer) to be read by the application. If this buffer is full, the new incoming packet cannot be queued and is consequently dropped. This buffer overflow situation is commonly encountered when an application doesn't read incoming data quickly enough, causing the buffer to fill up beyond its capacity. The "receive buffer errors" would then increment each time this happens.

Here's an example snippet from the output of netstat -su, where these values are typically listed:
ss -u -a : receive send queue size view usage.


Setting up network on host:
# Bring the interface down to make changes
sudo ip link set dev ens3 down

# Assign the IP address
sudo ip addr add 192.168.100.10/24 dev ens3

# Bring the interface up to enable networking
sudo ip link set dev ens3 up

# Add a default gateway if necessary
sudo ip route add default via 192.168.100.1

Need to bind to the tap device.

nc -l -p 1234
nc <host> <port>


echo 1 > /proc/sys/net/ipv4/ip_forward
sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
add route to gateway router to reach the router



https://man7.org/linux/man-pages/man7/packet.7.html
use eth_p_ip for all ip packets.
packet_outgoing for packets looped back in.
you can't use the IP_ADD_MEMBERSHIP on a raw packet.
you need to use ioctls to add the multicast group 


[jc@fedora hft]$ clang++ -O3 -o lll_strategy -luring lowlatencylab/client/mdmcclient.cpp lowlatencylab/client/L2OB.cpp && cat lll_strategy | nc 192.168.100.2 1234

rm lll_strategy || (g++ -o lll_strategy -luring lowlatencylab/client/mdmcclient.cpp lowlatencylab/client/mdmcclient.h lowlatencylab/client/L2OB.cpp lowlatencylab/client/L2OB.h lowlatencylab/defs.h lowlatencylab/client/Strat.h && nc 192.168.100.2 -p 1234)

sudo ./qemu-system-x86_64 -m 1024 -enable-kvm -drive if=virtio,file=test.qcow2,cache=none -cdrom ~/Downloads/Fedora-Workstation-Live-x86_64-38-1.6.iso   -netdev tap,id=net0,ifname=tap0,script=no,downscript=no   -device e1000e,netdev=net0,id=nic0




Thanks, that solved it.
So I have written a server and a client that work in userspace using xdp for low latency order placement. I have also modified the igb driver so that I can interact with the network card directly to receive packets as they come in via interrupts and write packets directly into the nic buffer. I would now like to modify the client strategy to utilize the functionality I have created in the modified driver.
The interface for the receive is void handleMDPacket(struct UDPMDPacket *packet, u16 packetSz). 
The interface for transmit is:
prepareWrite(cpu, tx_ring);
struct TCPPacket *pkt = getPacketBuffer(dmaSendBuffer);
preparePacket(pkt);
completeTx(tx_ring, pkt);

The client program is structured as follows: it configures the thread its running on to be a realtime thread and sets its affinity. then it runs a loop with a state machine with: Init -> oeconnect->running. it moves from init to oeconnect by initiating a tcp connection to an exchange server. Then it moves from oe connect to running once the tcp connection is established and in a
loop blocks on data. on receiving data it parses it, updates the order book and optionally fires a trade. it currently requests a buffer from the xdp umem to format its trades but I'd
like to change it to use the above interface to use the driver directly.
The marketdata server on receiving a tcp connection starts streaming data out to the connected client.
Now, in order to achieve this refactor there are a couple of things that need to change.
First the tcp establishment logic must be done. We need a trigger in this module to establish a client connection. This can most likely be done by the ethtool command line interface which we shall have to hack.
So on receiving an ethtool command to initiate a connection we transmit a tcp syn to the server. from then on the receive packet handler will route tcp messages on the oe connection 
to the tcp state machine and udp md packets to the strategy logic. the strategy logic can remain relatively unchanged and run as before.
The current oe holds a handle to a tcp object that does the sending. we can move the above interface into a tcp class and swing it. 
That tcp interface does offer a establish connection which is a blocking call. 
we are going to have to modify the logic to include the fact that we can no longer rely on blocking calls. 
the tcp connector will have to have a callback on the strategy object that moves it along its state machine to connected.
the problem here is that the strategy object is structured around these blocking calls with a looping control flow format.
We need to transform this into an interrupt driven control flow format. in order to move to this. we can preserve the blocking semantic by 
in its state machine adding more states that that indicate where its at. it must then route to its internal object that was previously doing the blocking call.
the internal object will return true or false to indicate whether the blocking call is done and the state machine can progress.


so lets start with the tcp handler. the main thing to update here is the XDPIO member variable.
This has the following interface.

handleFrames, getWriteBuf, cancelPrevWriteBuff, triggerWrite, complete, getReadDesc, releaseSingleFrame
the problematic ones are the blocking ones and the ones that take handlers.

releaseSingleFrame - this isnt used so can be deleted.
getReadDesc - also not used so can be delete.
complete - this is implemented by the driver in its poll logic. we should potentially be aware of this and trap the completions
           to tx queue we care about for the sake of keeping track of latencies. defer to the poll for cleanup.
triggerWrite - this is the moneyshot, will map to completetx. fairly straightforward. for us fire and forget for now track completions later as mentioned above.
cancelPrevWriteBuf - this one is a hopefully not too tricky and all we need to do is revert the next to use change.
getWriteBuf - also a relatively simple one that maps directly. Currently we obtain a chunk of memory and cycle through it. I wonder how the kernel slab allocator is for performance instead.
              We should get completions all bunched up together and we shouldn't ever run out of next to use by just proceeding linearly so theres no reason to use a general purpose allocator here.
              I think something that simply allocates the next available block in a txring should be good and allocate the memory upfront.
handleFrames - ok this is probably going to be a tricky one. so the interface is given a number of frames that are available, 
               retrieve the descriptors and run the provided handler on them. after that release all the frames. the provided handler is just a function pointer.
               so actually this is not so bad because there's no blocking logic, just some indirection which ultimately are all
              inlinable function calls.
recv - ok this is actually the only tricky one. all of these others dont require blocking calls. so this blocks waiting for
       a packet. the tricky thing about implementing this interface is basically we need to capture the current call stack
       and then when a packet comes in play it back from where it paused. you need to pause the coroutine and then when data is available
       continue its execution. This means that we need to manipulate all the callers of this function. we also can't do fake updates which we need to revert when trying to keep our hot path warm 
       because an interrupt will see inconcsistent data here. we would have to clone our datastructures potentially at the call sites or something...
       Luckily there are only two callers of this function. waitAck which is used at several points in establishing the tcp connection and the main client blocking receieve.
       at a time there is only going to be a single unambiguous listener for this received packets (ok we do get tcp acks etc. but the strategy arbitrates that). which means
       we can have a single slot that captures the state of the continuation. Continuations are modilled as (a -> r) -> r.
       So that's what we have to accept. the function calls into the blocking receive providing the continuation (a -> r).
       When we get a packet, we fire the continuation slot. Make sure this continuation slot is occupied on fire and also it is only updated once. make sure to let the compiler know when it is updated as well.
       you can use a const T and template out the slot? yeah and from the callsite call the correct template function. 
        
The other thing is going to be compiling without access to the standard library.
What that means is none of the stl which i do use a fair bit...




TODO - also need to adapt the tcpclient to use the new interface... need to adapt because when formatting tcp packets we need the sequence numbers.