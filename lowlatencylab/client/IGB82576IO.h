//
// Created by joshuacoutinho on 15/12/23.
//

#ifndef IGB82576IO_H
#define IGB82576IO_H

#include "../defs.h"
#include "../../cppkern/containers.h"

struct IGBConfig {
	constexpr static int NUM_READ_DESC = 256;
	constexpr static int NUM_WRITE_DESC = 256;
	constexpr static int FRAME_SIZE = (sizeof(OrderFrame) + 127) & 127;

	static_assert(sizeof(OrderFrame) <= 127);
	static_assert(NUM_WRITE_DESC * FRAME_SIZE <= (32 << 10));
	static_assert(__builtin_popcount(NUM_READ_DESC) == 1);
	static_assert(__builtin_popcount(NUM_WRITE_DESC) == 1);
};

class KmemTxSlotState {
	constexpr static int MASK = IGBConfig::NUM_WRITE_DESC - 1;
	static_assert(__builtin_popcount(IGBConfig::NUM_WRITE_DESC) == 1);

public:
	bitset<IGBConfig::NUM_WRITE_DESC> umemFrameState{};
	int front{0};

	u32 nextSlot() {
		assert(front < IGBConfig::NUM_WRITE_DESC);
		const int nextSlot = front;
		assert(!umemFrameState.test(nextSlot));
		umemFrameState.set(nextSlot);
		front = (front + 1) & MASK;
		return nextSlot * IGBConfig::FRAME_SIZE;
	}

	void previousSlot(u32 addr) {
		front = (front + MASK) & MASK;
		assert(front == (addr / IGBConfig::FRAME_SIZE));
		release(addr);
	}

	void release(u32 addr) {
		const u64 frameNo = addr / IGBConfig::FRAME_SIZE;
		umemFrameState.reset(frameNo);
	}

	void stateCheck() {
		assert(front < IGBConfig::NUM_WRITE_DESC);
		assert(front >= 0);
		int prev = ((front + MASK) & MASK);
		int pprev = ((prev + MASK) & MASK);
		assert(umemFrameState.get(prev) != 0 || umemFrameState.get(pprev));
	}
};

struct TxQueue {
	igb_ring* txQ;
	TxQueue(igb_ring* txQ):txQ{txQ} {
		assert(txQ->count == IGBConfig::NUM_WRITE_DESC);
		assert(txQ->tx_buffer_info);
	}

	void stateCheck() {
		int nta = txQ->next_to_alloc;
		int ntc = txQ->next_to_clean;
		int ntu = txQ->next_to_use;
		assert((ntc - nta) > 0 || (ntu - ntc) > 0);
	}

};

struct RxQueue {
	igb_ring* rxQ;
	RxQueue(igb_ring* rxQ): rxQ{rxQ} {
		assert(rxQ->count == IGBConfig::NUM_READ_DESC);
		assert(rxQ->tx_buffer_info);
	}
	void stateCheck() {
		int nta = rxQ->next_to_alloc;
		int ntc = rxQ->next_to_clean;
		int ntu = rxQ->next_to_use;
		assert((ntc - nta) > 0 || (ntu - ntc) > 0);
	}

};

struct Kmem {
	u8* dmaBuffer{};
	KmemTxSlotState slotState{};

	Kmem() {
		size_t totBuffSz = IGBConfig::FRAME_SIZE * IGBConfig::NUM_WRITE_DESC;
		assert(total_buffer_size < PAGE_SIZE);
		gfp_t dma_mem_flags = GFP_KERNEL;


		// TODO - use kmem_cache_create/alloc when doing multiple allocations instead
		dmaBuffer = static_cast<u8*>(kmalloc(totBuffSz, dma_mem_flags));
		assert(dmaSendBuffer); // fail eth tool here rather than crash the kernel it think.
		assert((reinterpret_cast<u64>(dmaBuffer) & 4095) == 0);

	}

	void stateCheck() {
		assert(dmaBuffer != nullptr);
		slotState.stateCheck();
	}

};

template<typename ContFunctor>
class IGB82576IO {
public:
	const char *devName;
	net_device* device;
	igb_adapter* adapter;
	int cpu;
	TxQueue tx;
	RxQueue rx;
	Kmem dmaSendBuffer;
	netdev_queue* nq;

	int intransit = 0;
	ContFunctor contSlot{};

	IGB82576IO(const char* devName, net* nets):
		devName{devName},
		device{dev_get_by_name(netns, devName)},
		adapter{netdev_priv(device)},
		cpu{smp_processor_id()},
		tx{adapter->tx_ring[cpu % adapter->num_tx_queues]},
		rx{adapter->rx_ring[cpu % adapter->num_rx_queues]},
		dmaSendBuffer{},
		nq{txring_txq(tx.txQ)}
	{

		assert(device);
		assert(adapter->flags == 0); // TODO - check later

		assert(adapter);
		assert(adapter->netdev == device);
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

		pr_info("Device Info: cpu qvec: %p, qvecs %d, tx %d, rx %d, min_frame %d, max_frame %d",
			tx.txQ->q_vector, adapter->num_q_vectors,
			adapter->num_tx_queues, adapter->num_rx_queues,
			adapter->min_frame_size, adapter->max_frame_size);
		assert(adapter->num_q_vectors ==
				 adapter->num_rx_queues +
					 adapter->num_tx_queues);
		assert(adapter->min_frame_size <= sizeof(struct UDPPacket));
		assert(adapter->max_frame_size >= sizeof(struct UDPPacket));

		// TODO assert num rings is the same as number of cpus

		pr_info("Running on cpu %d", cpu);
		assert(cpu >= 0 && cpu <= 0, "Unexpected large cpu");

		// TODO - what is the implication of this?

		// TODO - why is this not set? The driver does set this when creating a q vector. This ctx is supposed to indicate that there is a unique tx q per interrupt.

		//  assert(test_bit(IGB_RING_FLAG_TX_CTX_IDX, &tx_ring->flags), "No ctx");

	}


	void recv() {
		contSlot = f;
	}

	void handleFrames(struct igb_q_vector *qvec, int irq) {
		assert(contSlot.isSet());

		ErrorCode err{};
		const auto curTime = currentTimeNs();

		int available = 0;

		struct igb_ring *rx = (qvec->rx.ring);

		bool isFrag = false;

		int ogNTC = rx->next_to_clean;
		int ogNTU = rx->next_to_use;
		int ogNTA = rx->next_to_alloc;

		// TODO - is it faster to loop prefetching and then loop again going through all the packets?
		while(true) {
			u16 ntc = rx->next_to_clean;
			assert(ntc <= rx->count);

			union e1000_adv_rx_desc *descToClean =
				IGB_RX_DESC(rx, rx->next_to_clean);
			u16 packetSz = le16_to_cpu(descToClean->wb.upper.length);
			if (packetSz == 0) {
				break;
			}
			++available;
			assert(available <= rx->count);
			assert(rx->next_to_use - rx->next_to_clean >= 0 ||
					 rx->next_to_clean - rx->next_to_alloc >= 0);
			assert(packetSz == le16_to_cpu(descToClean->wb.upper.length));
			u32 stBits = descToClean->wb.upper.status_error;
			u32 status_error = stBits >> 19;
			u32 status = stBits & ~((u32)(-1) << 19);
			bool dd = status & 1;
			bool eop = (status >> 1) & 1;
			// TODO - this is a bug with the qemu hardware implementation - it does not set this. (grepping for PIF)
			// AFAIK - the driver doesn't use this either *shrug*, can test to see on real hardware though
			//    bool pif = (status >> 6) & 1;
			bool vp = (status >> 3) & 1;

			assert(dd == 1);
			if ((status_error) != 0) {
				pr_info("NIC reported error: %x",
					descToClean->wb.upper.status_error);
				continue;
			}
			bool packetFilters = false;
			bool b;

			if ((b = eop != 1)) {
				isFrag = true;
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
			packetFilters |= isFrag;

			isFrag = isFrag && (eop == 0);

			if (packetFilters) {
				pr_info("Bailing on irq %d, queue %d", irq,
					qvec->rx.ring->queue_index);
				continue;
			}

			dma_rmb();

			struct igb_rx_buffer *rxbuf = &(rx->rx_buffer_info[ntc]);
			prefetchw(rxbuf->page);
			auto page_offset = rxbuf->page_offset;
	        assert((page_offset & 255) == 0 && ( (page_offset - 256) & (IGBConfig::FRAME_SIZE - 1)) == 0);
			assert(page_offset < IGBConfig::FRAME_SIZE * (IGBConfig::NUM_WRITE_DESC - 1));

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

			assert(rxbuf->joshFlags == 0);
			rxbuf->joshFlags = 1 << JOSH_RX_PAGE_IN_CACHE_SHIFT;
			const auto handleErr = contSlot.handlePacket(qvec, packetSz, ntc);
            err.append(handleErr);
			rx->next_to_clean = (rx->next_to_clean + 1) % rx->count;
		}


		rx->next_to_clean = ogNTC;

		assert(ogNTC == rx->next_to_clean);
		assert(ogNTU == rx->next_to_use);
		assert(ogNTA == rx->next_to_alloc);

		contSlot.cont(err);
	}

	// TODO - dma buffers to nic in advance of triggering a write.
	// a slab allocator would be more effective herein terms of cache efficiency.
	u8 *getWriteBuff(ssize_t sz) {
		stateCheck();


		// TODO - we should not have to tx lock over here.
		__netif_tx_lock(nq, cpu);

		assert(!(test_bit(__IGB_DOWN, &adapter->state)));
		assert(!(test_bit(__IGB_TESTING, &adapter->state)));
		assert(!(test_bit(__IGB_RESETTING, &adapter->state)));
		assert(!(test_bit(__IGB_PTP_TX_IN_PROGRESS, &adapter->state)));

		assert(sz < IGBConfig::FRAME_SIZE);
		assert(intransit == 0);
		++intransit;
		assert(qs.txQ.mask != 0);

		const u32 addr = dmaSendBuffer.slotState.nextSlot();

		assert(nullptr != txDescr);

		u8* writeBuf = dmaSendBuffer.dmaBuffer + addr;
		return writeBuf;

	}

	void cancelPrevWriteBuff() {
		// make sure this is never called. we shall have to redefine things.
		assert(false);
	}

	template<typename T>
	void triggerWrite(const T& pkt) {
		stateCheck();
		assert(umem.txState.umemFrameState.any());
		assert(intransit == 1);
		--intransit;


		int txdIdx = tx.txQ->next_to_use;
		int txdUseCount = 2;

		assert(txdIdx < IGBConfig::NUM_WRITE_DESC);
		assert(igb_desc_unused(tx_ring) >= txdUseCount + 2);

		struct igb_tx_buffer *tx_head = &tx.txQ->tx_buffer_info[txdIdx];
		struct igb_tx_buffer *tx_buffer = tx_head;
		union e1000_adv_tx_desc *tx_desc = IGB_TX_DESC(tx.txQ, txdIdx);
		pr_info("Preparing packet txIx %d", txdIdx);

		void *data = static_cast<void*>(&pkt);
		int dataSz = sizeof(T);
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
			assert(false, "DMA mapping Error");

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

		netdev_tx_sent_queue(txring_txq(tx.txQ), tx_head->bytecount);

		tx_head->time_stamp = jiffies;
		smp_wmb();

		assert(txdIdx == tx_ring->next_to_use,
			 "Race condition - index doesn't match next to use");
		int nextIdx = (txdIdx + 1) % tx.txQ->count;
		tx_head->next_to_watch = tx_desc;
		tx.txQ->next_to_use = nextIdx;
		wmb();
		pr_info("Writing tail register to trigger send: dma %llu", dma);
		writel(nextIdx, tx.txQ->tail);
		// TODO What does igb_xdp_ring_update_tail do?
		__netif_tx_unlock(nq);

		// todo move this to the complete - need to trigger the complete on igb_poll which happens in a different thread though.
		dmaSendBuffer.slotState.release(data - dmaSendBuffer.dmaBuffer);
	}

	void complete() {
		stateCheck();

	}

	void stateCheck() {
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
		assert(adapter->stats.tncrs > 0); // should not be doing carrier sense

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
		assert(tx.stateCheck());
		assert(rx.stateCheck());
		assert(dmaSendBuffer.stateCheck());
	}
};

#endif //IGB82576IO_H
