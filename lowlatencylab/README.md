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
SO_RCVLOWAT and SO_SNDLOWAT
IORING_SETUP_SQPOLL in io_uring_setup

IORING_OP_SEND_ZC iouring flags

IP_TOS (since Linux 1.0)

Use POLLHUP - to determine when the socket has hung up



Use SIOCGSTAMP - for round trip time measurements.


For io_uring:
https://kernel.dk/io_uring.pdf

https://javadoc.lwjgl.org/org/lwjgl/system/linux/liburing/LibIOURing.html