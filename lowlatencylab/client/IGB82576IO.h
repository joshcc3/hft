//
// Created by joshuacoutinho on 15/12/23.
//

#ifndef IGB82576IO_H
#define IGB82576IO_H

#include "CoroutineMngr.h"
#include "defs.h"
#include "../../cppkern/containers.h"

#include "../../cppkern/IGB82576Interop.h"


using namespace josh;

struct IGBConfig {
    constexpr static int NUM_READ_DESC = 256;
    constexpr static int NUM_WRITE_DESC = 256;
    constexpr static int FRAME_SIZE = (sizeof(OrderFrame) + 127) & (~127);

    IGBConfig() {
        assert(sizeof(OrderFrame) <= 127);
        assert(NUM_WRITE_DESC * FRAME_SIZE <= (32 << 10));
        assert(__builtin_popcount(NUM_READ_DESC) == 1);
        assert(__builtin_popcount(NUM_WRITE_DESC) == 1);
    }
};

class KmemTxSlotState {
    constexpr static int MASK = IGBConfig::NUM_WRITE_DESC - 1;

public:
    bitset<IGBConfig::NUM_WRITE_DESC> umemFrameState{};
    int front{0};

    KmemTxSlotState() {
        assert(__builtin_popcount(IGBConfig::NUM_WRITE_DESC) == 1);
    }

    u32 nextSlot() {
        assert(front < IGBConfig::NUM_WRITE_DESC);
        const int nextSlot = front;
        assert(!umemFrameState.get(nextSlot));
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
        assert(umemFrameState.get(prev) == 1 || !umemFrameState.get(pprev));
    }
};

struct TxQueue {
    void* txQ;

    TxQueue(void* txQ): txQ{txQ} {
    }

    bool stateCheck() {
        int nta;
        int ntc;
        int ntu;
        q_get_ptrs(txQ, &nta, &ntc, &ntu);

        assert((ntc - nta) > 0 || (ntu - ntc) > 0);
        return true;
    }
};

struct RxQueue {
    void* rxQ;

    RxQueue(void* rxQ): rxQ{rxQ} {
    }

    bool stateCheck() {
        int nta;
        int ntc;
        int ntu;
        q_get_ptrs(rxQ, &nta, &ntc, &ntu);
        assert((ntc - nta) > 0 || (ntu - ntc) > 0);
        return true;
    }
};

struct Kmem {
    u8* dmaBuffer{};
    KmemTxSlotState slotState{};

    Kmem() {
        size_t totBuffSz = IGBConfig::FRAME_SIZE * IGBConfig::NUM_WRITE_DESC;


        // TODO - use kmem_cache_create/alloc when doing multiple allocations instead
        dmaBuffer = static_cast<u8 *>(malloc(totBuffSz));
        assert(dmaBuffer); // fail eth tool here rather than crash the kernel it think.
        assert((reinterpret_cast<u64>(dmaBuffer) & 4095) == 0);
    }

    bool stateCheck() {
        assert(dmaBuffer != nullptr);
        slotState.stateCheck();
        return true;
    }
};

enum NICPktStatus {
    INVALID_PKT,
    PKT_ERROR,
    OK,
    ALREADY_PROCESSED,
};

struct SyncResult {
    NICPktStatus pktStatus;
    u16 pktSize;
    bool isFrag;
};

template<typename X, typename Y>
class IGB82576IO {
public:
    using PacketHandler = ErrorCode (*)(IOBlockers<X, Y>& blocker, const u8* pkt, u32 pktSz);

    // TODO - need to ensure that the nic only gets called on our cpu dedicated to packet handling.

    bool acceptingPkts;
    void* adapter;
    int cpu;
    TxQueue tx;
    RxQueue rx;
    Kmem dmaSendBuffer;
    void* nq;
    ContextMgr& ctxM;

    IOBlockers<X, Y> blocker;
    PacketHandler handler;

    int intransit = 0;


    IGB82576IO(void* adapter, void* rx, ContextMgr& ctxM): adapter{adapter},
                                                                     cpu{getCpu()},
                                                                     tx{
                                                                         getTxRing(adapter, cpu,
                                                                             IGBConfig::NUM_WRITE_DESC)
                                                                     },
                                                                     rx{rx},
                                                                     dmaSendBuffer{},
                                                                     nq{getNetdevQ(tx.txQ)},
                                                                     ctxM{ctxM},
                                                                     acceptingPkts{false} {

        // TODO assert num rings is the same as number of cpus
	    assert(adapter);
	    assert(rx);
	    assert(nq);
        pr_info__("Running on cpu %d", cpu);
        assert(cpu >= 0 && cpu <= 0);

        // TODO - what is the implication of this?

        // TODO - why is this not set? The driver does set this when creating a q vector. This ctx is supposed to indicate that there is a unique tx q per interrupt.

        //  assert(test_bit(IGB_RING_FLAG_TX_CTX_IDX, &tx_ring->flags), "No ctx");
    }


    void recv(IOBlockers<X, Y>& blocker, PacketHandler handler) {
        this->blocker = blocker;
        this->handler = handler;
		pr_info__("blocking on recv - waiting for packets");
        swapcontext_(&ctxM.blockingRecvCtx, &ctxM.interruptCtx);
		// tODO set blocker to null.
		pr_info__("pkts processed, proceeding");
    }

    // TODO - dma buffers to nic in advance of triggering a write.
    // a slab allocator would be more effective herein terms of cache efficiency.
    u8* getWriteBuff(ssize_t sz) {
        stateCheck();

        lockWriteBuf(adapter, nq, cpu);

        assert(sz < IGBConfig::FRAME_SIZE);
        assert(intransit == 0);
        ++intransit;

        const u32 addr = dmaSendBuffer.slotState.nextSlot();

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
        int txdIdx = ring_get_ntu(tx.txQ);

		//pr_info__("not proceeding on trigger write slot state %d, intransit %d, txIDx %d", dmaSendBuffer.slotState.umemFrameState.any(), intransit, txdIdx);
		//swapcontext_(&ctxM.blockingRecvCtx, &ctxM.interruptCtx);
        assert(dmaSendBuffer.slotState.umemFrameState.any());
        assert(intransit == 1);
        --intransit;


        u8* data = (u8 *)(&pkt);
        int dataSz = sizeof(T);

        assert(txdIdx < IGBConfig::NUM_WRITE_DESC);

		//pr_info__("Trigger write bail");
		//swapcontext_(&ctxM.blockingRecvCtx, &ctxM.interruptCtx);
        trigger_write(tx.txQ, nq, txdIdx, data, dataSz);
		//pr_info__("Trigger write successful ctx switch back");
		//swapcontext_(&ctxM.blockingRecvCtx, &ctxM.interruptCtx);


        // todo move this to the complete - need to trigger the complete on igb_poll which happens in a different thread though.
        dmaSendBuffer.slotState.release(data - dmaSendBuffer.dmaBuffer);
    }

    void complete() {
        stateCheck();
    }

    void stateCheck() {
        state_check(adapter, cpu);
        assert(tx.stateCheck());
        assert(rx.stateCheck());
        assert(dmaSendBuffer.stateCheck());
    }
};


#endif //IGB82576IO_H
