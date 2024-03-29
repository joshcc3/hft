//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_UDPBUFFER_H
#define LLL_EXCHANGE_UDPBUFFER_H

#include <cstring>

#include "defs.h"
#include "../../cubist/RingBuffer.h"

template<typename Derived>
struct PacketProcessor {
    void operator()(TimeNs t, const MDPayload& p) {
        static_cast<Derived *>(this)->processPacket(t, p);
    }
};

template<typename PP>
struct UDPBuffer {
    using MaskType = u64;
    constexpr static int Sz = 64;
    constexpr static int UNPROCESSED_SZ = (1 << 12);
    constexpr static MaskType ONES = ~MaskType(0);
    static_assert(__builtin_popcount(Sz) == 1);
    static_assert(Sz == 64);
    static_assert(ONES == 0xFFFFFFFFFFFFFFFF);

    using BufferItem = MDPayload;
    MaskType mask = 0;
    SeqNo nextMissingSeqNo = 0;
    u32 head = 0;
    BufferItem* ringBuffer = static_cast<BufferItem*>(malloc(sizeof(BufferItem) * Sz));
    RingBuffer<MDPayload, UNPROCESSED_SZ> unprocessed;

    [[nodiscard]] bool stateCheck() const {
        assert(head >= 0 && head < Sz);
        assert((mask & (MaskType(1) << head)) == 0);
        assert(nextMissingSeqNo >= 0);
        assert(unprocessed.empty() || nextMissingSeqNo < unprocessed.get(0).seqNo);
        for (int i = 0; i < Sz; ++i) {
            if (test(i)) {
                SeqNo seqNo = get(i).seqNo;
                assert(seqNo != -1);
                assert(seqNo > nextMissingSeqNo);
                assert(unprocessed.empty() || seqNo < unprocessed.get(0).seqNo);
            }
        }
        for (int i = 0; i < unprocessed.size(); ++i) {
            const auto& packet = unprocessed.get(i);
            assert(packet.seqNo != -1);
            assert(packet.seqNo - nextMissingSeqNo - Sz < UNPROCESSED_SZ);
        }

        return true;
    }

    [[nodiscard]] const MDPayload& get(int i) const {
        assert(i >= 0 && i < Sz);
        u32 bufferIx = (head + i) & (Sz - 1);
        assert((mask >> bufferIx) & 1);
        return ringBuffer[bufferIx];
    }

    [[nodiscard]] bool test(int pos) const {
        assert(pos < Sz && pos >= 0);
        return (mask >> ((head + pos) & (Sz - 1))) & 1;
    }


    int newMessage(TimeNs time, const MDPayload& msg, PP& packetProcessor) {
        SeqNo seqNo = msg.seqNo;
            assert(stateCheck());
            assert(head < Sz);
            assert(nextMissingSeqNo >= 0);
            assert(mask != ONES);

            assert(seqNo >= nextMissingSeqNo);
        u32 bufferOffs = seqNo - nextMissingSeqNo;
        int totalPacketsProccessed = -1;

        if (__builtin_expect(bufferOffs < Sz, true)) {
            u32 bufferPos = (head + bufferOffs) & (Sz - 1);
            MaskType maskBit = MaskType(1) << bufferPos;
            assert((mask & maskBit) == 0);


            mask |= maskBit;

            if (seqNo == nextMissingSeqNo) {
	      int packetsToProcess;
		packetProcessor(time, msg);
                // CLOCK(MSG_HANDLING_PC,
		      int maxFull = 1;
		      MaskType alignedMask = rotr(mask, head);
		      assert(rotl(alignedMask, head) == mask);
		      assert(alignedMask & 1);
		      for (MaskType fullMask = 1;
			   maxFull < Sz && (fullMask & alignedMask) == fullMask;
			   ++maxFull, fullMask = nOnes(maxFull));
		      packetsToProcess = maxFull - 1;
		      // )
		  for (int j = 1; j < packetsToProcess; ++j) {
                    const MDPayload& p = get(j);
                    packetProcessor(time, p);
                }
		
                // CLOCK(MSG_HANDLING_PC,
		      advance(packetsToProcess);
		      totalPacketsProccessed = packetsToProcess;
		      
		      if (!unprocessed.empty() && unprocessed.front().seqNo == nextMissingSeqNo) {
			const MDPayload p = unprocessed.front();
			unprocessed.pop();
			totalPacketsProccessed += newMessage(time, p, packetProcessor);
		      }
		      // )

            } else {
	      new(&ringBuffer[bufferPos]) BufferItem{msg};
	      assert(ringBuffer[bufferPos].seqNo == msg.seqNo);
	      assert(ringBuffer[bufferPos].price == msg.price);
	      assert(ringBuffer[bufferPos].localTimestamp == msg.localTimestamp);
	      totalPacketsProccessed = 0;
            }
        } else if (msg.seqNo - nextMissingSeqNo - Sz < UNPROCESSED_SZ) {
            unprocessed.emplace_back(msg);
            totalPacketsProccessed = 0;
        } else {
            printf__("Book update [%f].\n", timeSpent[1] / timeSpent[0] * 100);
            printf__("Order Submission [%f].\n", timeSpent[2] / timeSpent[0] * 100);
            printf__("Message Handling [%f].\n", timeSpent[3] / timeSpent[0] * 100);

            printf__("Message gap detected: %d\n", (msg.seqNo - nextMissingSeqNo - Sz));

            assert(false);
        }

        assert(stateCheck());

        assert(totalPacketsProccessed != -1);
        return totalPacketsProccessed;
    }

    void advance(int n) {
        MaskType alignedMask = rotr(mask, head);
        assert(rotl(alignedMask, head) == mask);
        MaskType fullMask = nOnes(n);
        assert(__builtin_popcount(fullMask) == n);
        assert((fullMask & alignedMask) == fullMask);
        MaskType updatedAlignedMask = alignedMask & ~fullMask;
        mask = rotl(updatedAlignedMask, head);
        head = (head + n) & (Sz - 1);
        nextMissingSeqNo += n;
    }

    MaskType __attribute__((always_inline)) nOnes(int n) {
        return ~(~MaskType(0) << n); // this creates a bitvector with the first n bits as 1.
    }
};

#endif //LLL_EXCHANGE_UDPBUFFER_H
