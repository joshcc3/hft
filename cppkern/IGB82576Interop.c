#include "IGB82576Interop.h"

#include <linux/kernel.h>
#include <linux/stdarg.h>
#include <linux/printk.h>
#include <linux/netdevice.h>
#include <linux/smp.h>
#include <linux/prefetch.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/gfp_types.h>
#include <linux/byteorder/generic.h>

#include <asm-generic/barrier.h>
#include <asm-generic/io.h>

#include "../../igb.h"

void bug() {
  BUG();
}

void* malloc(long unsigned int sz) {
    gfp_t flags = GFP_KERNEL;
    return kmalloc(sz, flags);
}


void ssleep__(unsigned int seconds) {
	ssleep(seconds);
}


u8 syncPacket(void* rx_, u16* pktSz, u8* isFrag, int frameSz, int numDesc) {
    struct igb_ring* rx = rx_;

    assert(rx->count);

    u16 ntc = rx->next_to_clean;
    assert(ntc <= rx->count);

    union e1000_adv_rx_desc* descToClean =
            IGB_RX_DESC(rx, rx->next_to_clean);
    u16 packetSz = le16_to_cpu(descToClean->wb.upper.length);
    if (packetSz == 0) {
        *pktSz = packetSz;
        return 0;
    }
    pr_info("ntu %d, ntc %d, nta %d, pktSz %d, %d", rx->next_to_use, rx->next_to_clean, rx->next_to_alloc, packetSz, le16_to_cpu(descToClean->wb.upper.length));
    assert(rx->next_to_use - rx->next_to_clean >= 0 ||
        rx->next_to_clean - rx->next_to_alloc >= 0);
    assert(packetSz == le16_to_cpu(descToClean->wb.upper.length));
    u32 stBits = descToClean->wb.upper.status_error;
    u32 status_error = stBits >> 19;
    u32 status = stBits & ~((u32) (-1) << 19);
    bool dd = status & 1;
    bool eop = (status >> 1) & 1;
    // TODO - this is a bug with the qemu hardware implementation - it does not set this. (grepping for PIF)
    // AFAIK - the driver doesn't use this either *shrug*, can test to see on real hardware though
    //    bool pif = (status >> 6) & 1;
    bool vp = (status >> 3) & 1;
    pr_info("Status bits %x", status);
    if ((status_error) != 0) {
        pr_info("NIC reported error: %x",
                descToClean->wb.upper.status_error);
        *pktSz = packetSz;
        return 1;
    }
    assert(dd == 1);
    bool packetFilters = false;
    bool b;

    if ((b = eop != 1)) {
        *isFrag = true;
        packetFilters |= b;
        pr_info("Skipping fragmented packets");
    }
    // TODO Why is pif set?
    /*if((b = pif == 1)) {
  packetFilters |= b;
  pr_info("Packets not destined for us");

  }*/
    if ((b = vp == 1)) {
        packetFilters |= b;
        pr_info("Vlan packet");
    }

    if ((b = dd != 1)) {
        packetFilters |= b;
        pr_info("Descriptor Done not set");
    }
    if ((b = packetSz == 0)) {
        packetFilters |= b;
        pr_info("LEngth is 0");
    }
    packetFilters |= *isFrag;

    *isFrag = *isFrag && (eop == 0);

    if (packetFilters) {
        *pktSz = packetSz;
        return 1;
    }

    pr_info("Observed legit packet ntc %d, rx %p", ntc, rx);
    dma_rmb();

    struct igb_rx_buffer* rxbuf = &(rx->rx_buffer_info[ntc]);
    if(((rxbuf->joshFlags >> JOSH_RX_PAGE_PROCESSED_SHIFT) & 1) == 1) {
	pr_info("Skipping already processed packet");
        return 3;
    }

    // TODO - only prefetch and sync after checking flags. we do call sync multiple times.
    pr_info("Doing prefetch %p", rxbuf);
    if(((rxbuf->joshFlags >> JOSH_RX_PAGE_IN_CACHE_SHIFT) & 1) == 0) {
        prefetchw(rxbuf->page);
        int page_offset = rxbuf->page_offset;
        pr_info("page struct sz %d, Page data %d frame %d numDesc %d", (int)sizeof(struct page), page_offset, frameSz, numDesc);
        // Page offset in the page - likely the page offset is after the page struct metadata.
        assert((page_offset & 63) == 0 && ( (page_offset - 192) & (frameSz - 1)) == 0);
        assert(page_offset <= frameSz * (numDesc - 1));

        pr_info("dma %llu l1cache %d", rxbuf->dma, L1_CACHE_BYTES);
        /*
    Memory coherency operates at a granularity called the cache
    line width.  In order for memory mapped by this API to operate
    correctly, the mapped region must begin exactly on a cache line
    boundary and end exactly on one (to prevent two separately mapped
    regions from sharing a single cache line).  Since the cache line size
    may not be known at compile time, the API will not enforce this
    requirement.  Therefore, it is recommended that driver writers who
    don't take special care to determine the cache line size at run time
    only map virtual regions that begin and end on page boundaries (which
    are guaranteed also to be cache line boundaries).

    DMA_TO_DEVICE synchronisation must be done after the last modification
    of the memory region by the software and before it is handed off to
    the device.  Once this primitive is used, memory covered by this
    primitive should be treated as read-only by the device.  If the device
    may write to it at any point, it should be DMA_BIDIRECTIONAL (see
    below).

    DMA_FROM_DEVICE synchronisation must be done before the driver
    accesses data that may be changed by the device.  This memory should
    be treated as read-only by the driver.  If the driver needs to write
    to it at any point, it should be DMA_BIDIRECTIONAL (see below).

    DMA_BIDIRECTIONAL requires special handling: it means that the driver
    isn't sure if the memory was modified before being handed off to the
    device and also isn't sure if the device will also modify it.  Thus,
    you must always sync bidirectional memory twice: once before the
    memory is handed off to the device (to make sure all memory changes
    are flushed from the processor) and once before the data may be
    accessed after being used by the device (to make sure any processor
    cache lines are updated with data that the device may have changed).
         */
        assert((rxbuf->dma & (L1_CACHE_BYTES - 1)) == 0);
        dma_sync_single_range_for_cpu(rx->dev, rxbuf->dma,
                                      page_offset, packetSz,
                                      DMA_FROM_DEVICE);

        rxbuf->joshFlags |= 1 << JOSH_RX_PAGE_IN_CACHE_SHIFT;
    }
    assert((rxbuf->joshFlags & (1 << JOSH_RX_PAGE_PROCESSED_SHIFT)) == 0);

    *pktSz = packetSz;
    return 2;
}

u8* getOurPacket(void* rx_) {
    struct igb_ring* rx = rx_;
    u16 ntc = rx->next_to_clean;
    // assert(qvec->cpu == 0);

    struct igb_rx_buffer* rxbuf = &(rx->rx_buffer_info[ntc]);
    struct page* dataPage = rxbuf->page;
    assert(rxbuf->page_offset < PAGE_SIZE);
    u8* pkt = (u8 *) ((page_address(dataPage) +
                       rxbuf->page_offset));

    const struct TCPFrame* tcpPacket = (struct TCPFrame *) (pkt);
    const struct UDPFrame* udpPacket = (struct UDPFrame *) (pkt);
    const bool isIP = udpPacket->eth.h_proto == htons(ETH_P_IP);
    // TODO Filter this on the incoming port.
    bool isStratPkt = isIP & (((udpPacket->ip.protocol == 17) & (ntohs(udpPacket->udp.dest) == MD_UNICAST_PORT)) ||
                              ((tcpPacket->ip.protocol == 6) & (ntohs(tcpPacket->tcp.source) == OE_PORT)));
    if (isStratPkt) {
        rxbuf->joshFlags |= (1 << JOSH_RX_PAGE_PROCESSED_SHIFT);
    }
    return isStratPkt ? pkt : NULL;
}


void getPointers(void* rx_, int* ogNTC, int* ogNTU, int* ogNTA, int* count) {
    struct igb_ring* rx = rx_;

    *ogNTC = rx->next_to_clean;
    *ogNTU = rx->next_to_use;
    *ogNTA = rx->next_to_alloc;
    *count = rx->count;
}

void incNTC(void* rx_) {
    struct igb_ring* rx = rx_;
    rx->next_to_clean = (rx->next_to_clean + 1) % rx->count;
}

void setNTC(void* rx_, int ntc) {
    struct igb_ring* rx = rx_;
    rx->next_to_clean = ntc;
}


void lockWriteBuf(void *adapter_, void* nq_, int cpu) {
    struct netdev_queue* nq = nq_;
    struct igb_adapter* adapter = adapter_;

    // TODO - we should not have to tx lock over here.
    __netif_tx_lock(nq, cpu);

    assert(!(test_bit(__IGB_DOWN, &adapter->state)));
    assert(!(test_bit(__IGB_TESTING, &adapter->state)));
    assert(!(test_bit(__IGB_RESETTING, &adapter->state)));
    assert(!(test_bit(__IGB_PTP_TX_IN_PROGRESS, &adapter->state)));
}


u8 isStartCmdPacket(void* rx_) {
    struct igb_ring* rx = rx_;
    u16 ntc = rx->next_to_clean;
    // assert(rx->count != 0);
    // assert(rx->total_bytes == 0);
    // assert(rx->total_packets == 0);

    struct igb_rx_buffer* rxbuf = &(rx->rx_buffer_info[ntc]);
    struct page* dataPage = rxbuf->page;
    assert(rxbuf->page_offset < PAGE_SIZE);
    pr_info("DataPage %p", (void*)dataPage);
    struct MDFrame* packet = page_address(dataPage) + rxbuf->page_offset;
    u8 isIP = packet->eth.h_proto == htons(ETH_P_IP);

    // TODO Filter this on the incoming port.
    u8 isCMDPacket = isIP && packet->ip.protocol == 17 &&
                     packet->udp.dest == htons(CMD_PORT);
    pr_info("eth %x proto %d dest %d | iscmd %d", ntohs(packet->eth.h_proto), packet->ip.protocol, ntohs(packet->udp.dest), isCMDPacket);
    if (isCMDPacket) {
        rxbuf->joshFlags |= (1 << JOSH_RX_PAGE_PROCESSED_SHIFT);
    }
    return isCMDPacket;
}


/*
*	net_device* device;
 igb_adapter* adapter;
 int cpu;
 TxQueue tx;
 RxQueue rx;
 Kmem dmaSendBuffer;
 netdev_queue* nq;
 ContextMgr& ctxM;
 struct igb_ring* tx_ring;
 */

void trigger_write(void* txQ_, void* nq_, int txdIdx, void* data, int dataSz) {
    int txdUseCount = 2;

    struct igb_ring* tx_ring = txQ_;
    struct igb_ring* txQ = txQ_;
    struct netdev_queue* nq = nq_;


    assert(igb_desc_unused(tx_ring) >= txdUseCount + 2);

    struct igb_tx_buffer* tx_head = &txQ->tx_buffer_info[txdIdx];
    struct igb_tx_buffer* tx_buffer = tx_head;
    union e1000_adv_tx_desc* tx_desc = IGB_TX_DESC(txQ, txdIdx);
    pr_info("Preparing packet txIx %d", txdIdx);

    tx_head->bytecount = dataSz;
    tx_head->gso_segs = 1;
    tx_head->xdpf = NULL; // TODO - make sure this is not used anywhere else.

    u32 olinfo_status = tx_head->bytecount << E1000_ADVTXD_PAYLEN_SHIFT;
    // TODO - this should be set but the ctx_dx bit is not set in the flags for some reason
    olinfo_status |= tx_ring->reg_idx << 4;
    tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);

    // TODO - configure and use dca instead.
    pr_info("DMAing Data %d", dataSz);
    dma_addr_t dma;
    dma = dma_map_single(tx_ring->dev, data, dataSz, DMA_TO_DEVICE);
    if (dma_mapping_error(tx_ring->dev, dma))
        assert(false);

    /* record length, and DMA address */
    dma_unmap_len_set(tx_buffer, len, dataSz);
    dma_unmap_addr_set(tx_buffer, dma, dma);
    // TODO: ZC update to a data location?

    // TODO enable tcp/ip checksum offload. set TXSM, IXSM bits.
    int cmd_type = E1000_ADVTXD_DTYP_DATA | E1000_ADVTXD_DCMD_DEXT |
                   E1000_ADVTXD_DCMD_IFCS | E1000_ADVTXD_DCMD_EOP | dataSz;

    tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);
    tx_desc->read.buffer_addr = cpu_to_le64(dma);

    tx_buffer->protocol = 0;
    tx_desc->read.cmd_type_len |= cpu_to_le32(IGB_TXD_DCMD);

    netdev_tx_sent_queue(txring_txq(txQ), tx_head->bytecount);

    tx_head->time_stamp = jiffies;
    smp_wmb();

    assert(txdIdx == tx_ring->next_to_use);
    int nextIdx = (txdIdx + 1) % txQ->count;
    tx_head->next_to_watch = tx_desc;
    txQ->next_to_use = nextIdx;
    wmb();
    pr_info("Writing tail register to trigger send: dma %llu", dma);
    writel(nextIdx, txQ->tail);
    // TODO What does igb_xdp_ring_update_tail do?
    __netif_tx_unlock(nq);
}

void checkAdapter(void* adapter_) {
    struct igb_adapter* adapter = adapter_;
    assert(adapter->flags == 0); // TODO - check later

    assert(adapter);
    assert(adapter->num_q_vectors >= 3); // seperate interrupts for rx and tx and there is another one.
    // assert(adapter->rx_itr_setting == MAX_ITR);
    // assert(adapter->tx_itr_setting == MAX_ITR);

    // we might want to eventually split rx flow into multiple queues based on packet values or even drop packets
    assert(adapter->num_tx_queues >= 1);
    assert(adapter->num_rx_queues >= 1);
    // adapter->hw.phys_info.smart_speed - should check this value
    assert(adapter->num_rx_queues == adapter->rx_ring_count);
    assert(adapter->num_tx_queues == adapter->tx_ring_count);
    assert(adapter->link_duplex == 1);

    // TODO MYASSERT(cpu == PINNED_CPU);
    assert(adapter->num_tx_queues <= 16);

    pr_info("Device Info: qvecs %d, tx %d, rx %d, min_frame %d, max_frame %d",
             adapter->num_q_vectors,
            adapter->num_tx_queues, adapter->num_rx_queues,
            adapter->min_frame_size, adapter->max_frame_size);
    assert(adapter->num_q_vectors ==
        adapter->num_rx_queues +
        adapter->num_tx_queues);
    assert(adapter->min_frame_size <= sizeof(struct MDFrame) + 200);
    assert(adapter->max_frame_size >= sizeof(struct MDFrame) + 200);
}


void state_check(void* adapter_, int cpu) {
    struct igb_adapter* adapter = adapter_;
    assert(smp_processor_id() == cpu);
    // adapter->last_rx_timestamp - get hardware timestamps here
    assert(adapter->tx_hwtstamp_skipped == 0);
    assert(adapter->tx_hwtstamp_timeouts == 0);
    assert(adapter->state == 0); // TODO - check later
    assert(adapter->stats64.rx_dropped == 0);
    assert(adapter->stats64.tx_dropped == 0);
    assert(adapter->stats64.collisions == 0);
    assert(adapter->stats64.rx_missed_errors == 0);
    assert(adapter->stats64.tx_fifo_errors == 0);
    assert(adapter->stats.rxerrc == 0);
    assert(adapter->stats.mpc == 0); // missed packets
    assert(adapter->stats.colc == 0); // collision counts - should be using duplex
    // TODO: The emulated nic does not fill this counter, it should
    // assert(adapter->stats.tncrs > 0); // should not be doing carrier sense

    // adapter-.hw_stats has a bunch of useful statistics
    /*
     *crcerrs: Count of cyclic redundancy check (CRC) errors.

algnerrc: Alignment errors count.

symerrs: Symbol errors count.

rxerrc: Receive errors count.

mpc: Missed packets count.

scc: Single collision count.

ecol: Excessive collision count.

mcc: Multiple collision count.

latecol: Late collision count.

colc: Collision count.

dc: Defer count.

tncrs: Transmit with no carrier sense.

sec: Sequence error count.

cexterr: Carrier extension error count.

rlec: Receive length error count.

xonrxc: XON received count.

xontxc: XON transmitted count.

xoffrxc: XOFF received count.

xofftxc: XOFF transmitted count.

fcruc: Flow control receive unsupported count.

prc64: Packets received (64 bytes) count.

prc127, prc255, prc511, prc1023, prc1522: Packets received in different size ranges (up to 1522 bytes).

gprc: Good packets received count.

bprc: Broadcast packets received count.

mprc: Multicast packets received count.

gptc: Good packets transmitted count.

gorc: Good octets received count.

gotc: Good octets transmitted count.

rnbc: Receive no buffer count.

ruc: Receive undersize count.

rfc: Receive fragment count.

roc: Receive oversize count.

rjc: Receive jabber count.

mgprc, mgpdc, mgptc: Management packets received, dropped, and transmitted counts.

tor: Total octets received.

tot: Total octets transmitted.

tpr: Total packets received.

tpt: Total packets transmitted.

ptc64, ptc127, ptc255, ptc511, ptc1023, ptc1522: Packets transmitted in different size ranges.

mptc: Multicast packets transmitted count.

bptc: Broadcast packets transmitted count.

tsctc, tsctfc: TCP segmentation context transmitted and failed count.

iac: Interrupt assertion count.

icrxptc, icrxatc, ictxptc, ictxatc: Interrupt cause receive/transmit packet and absolute timer counts.

ictxqec, ictxqmtc: Interrupt cause transmit queue empty and minimum threshold counts.

icrxdmtc: Interrupt cause receiver descriptor minimum threshold count.

icrxoc: Interrupt cause receiver overrun count.

cbtmpc, htdpmc, cbrdpc, cbrmpc: Various counts related to flow control and packet buffering.

rpthc, hgptc, htcbdpc, hgorc, hgotc: High priority and host generated packets and octets counts.

lenerrs: Length errors count.

scvpc: Sequence control violation packet count.

hrmpc: Host received multicast packets count.

doosync: Doorbell overflow OOB sync error count.

o2bgptc, o2bspc, b2ospc, b2ogprc: Various counts related to out-of-band and in-band packet handling.
     */
}


/*
*		device{dev_get_by_name(netns, devName)},
		adapter{netdev_priv(device)},
		cpu{smp_processor_id()},
		tx{adapter->tx_ring[cpu % adapter->num_tx_queues]},
		rx{adapter->rx_ring[cpu % adapter->num_rx_queues]},
		dmaSendBuffer{},
		nq{txring_txq(tx.txQ)},

bool acceptingPkts;
	const char *devName;
	net_device* device;
	igb_adapter* adapter;
	int cpu;
	TxQueue tx;
	RxQueue rx;
	Kmem dmaSendBuffer;
	netdev_queue* nq;
	ContextMgr& ctxM;
	struct igb_ring* tx_ring;

 */

void* getDevice(const char* devName) {
    return dev_get_by_name(&init_net, devName);
}

void* getAdapter(void* device) {
    return netdev_priv((struct net_device *) device);
}

int getCpu() {
    return smp_processor_id();
}

void* getTxRing(void* adapter, int cpu, int numTxDesc) {
    struct igb_ring* txQ = ((struct igb_adapter *) adapter)->tx_ring[cpu % ((struct igb_adapter *) adapter)->num_tx_queues];
    assert(txQ->count == numTxDesc);
    return txQ;
}

void* getNetdevQ(void* txRing) {
    return txring_txq((struct igb_ring *) txRing);
}

void q_get_ptrs(void* q_, int* nta, int* ntc, int* ntu) {
    struct igb_ring* q = q_;
    *nta = q->next_to_alloc;
    *ntc = q->next_to_clean;
    *ntu = q->next_to_use;
}

int ring_get_ntu(void* ring) {
    struct igb_ring* txQ = ring;
    return txQ->next_to_use;
}

u64 currentTimeNs() {
    return ktime_get_real_ns();
}

void __cxa_pure_virtual() {}

void __cxa_guard_release() {}

bool __cxa_guard_acquire() {
	return true;
}


void pr_info__(const char *fmt, ...) {
	va_list args;
	char buffer[256];

	va_start(args, fmt); // Initialize the va_list with the last fixed parameter

	vsnprintf(buffer, sizeof(buffer), fmt, args);

	va_end(args);

	pr_info("josh:: %s", buffer);
}

[[noreturn]] void __attribute__((cold)) panic__(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
	panic("Critical Error");
}

void printf__(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

#ifndef __cplusplus
void __assert_fail() {
	BUG();
}

i32 __popcountdi2(i64 a) {
	u64 x2 = (u64)a;
	x2 = x2 - ((x2 >> 1) & 0x5555555555555555uLL);
	// Every 2 bits holds the sum of every pair of bits (32)
	x2 = ((x2 >> 2) & 0x3333333333333333uLL) + (x2 & 0x3333333333333333uLL);
	// Every 4 bits holds the sum of every 4-set of bits (3 significant bits) (16)
	x2 = (x2 + (x2 >> 4)) & 0x0F0F0F0F0F0F0F0FuLL;
	// Every 8 bits holds the sum of every 8-set of bits (4 significant bits) (8)
	u32 x = (u32)(x2 + (x2 >> 32));
	// The lower 32 bits hold four 16 bit sums (5 significant bits).
	//   Upper 32 bits are garbage
	x = x + (x >> 16);
	// The lower 16 bits hold two 32 bit sums (6 significant bits).
	//   Upper 16 bits are garbage
	return (x + (x >> 8)) & 0x0000007F; // (7 significant bits)
}


#endif