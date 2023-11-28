why is the packet type receiving 111

the headers seem somewhat accurate - this is likely being produced by the sendto


using xdp, we do a transmit (that transmit is the data that we send - as determined by not submitting and seeing nothing)
we see this in the xdp log:

              nc-110260  [001] ..s21 77437.969929: bpf_trace_printk: Packet: 0, type 1
              nc-110260  [001] ..s21 77437.969932: bpf_trace_printk: Forwarding marketdata packet
           <...>-110253  [003] ..s21 77437.970004: bpf_trace_printk: Packet: 0, type 111

Indicating that md packet is received and a subsequent order entry (type 111 which is incorrect though)
packet has also been seen.
I'm seeing the packet also in tcpdump now indicating that the port adn everything is correct.
however it seems to be being dropped just after tcpdump