Notes on the linux kernel

tracepoints in /sys/kernel/debug/tracing/events
sudo bpftrace -e 'tracepoint:net:netif_receive_skb /str(args->name) == "lo"/ { printf("%s: %s\n", comm, str(args->name)); }'


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