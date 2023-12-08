//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_STRAT_H
#define LLL_EXCHANGE_STRAT_H


#include <cstring>
#include <sched.h>
#include <new>
#include <cassert>
#include "OE.h"
#include "../defs.h"
#include "L2OB.h"
#include "mdmcclient.h"
#include "UDPBuffer.h"
#include "XDPIO.h"

class Strat : public PacketProcessor<Strat> {
public:
    static constexpr u32 READ_BUF_SZ = 1 << 12;

    bool isComplete = false;

    XDPIO& io;
    L2OB& ob;
    OE& orderEntry;

    UDPBuffer<PacketProcessor> udpBuf{};
    SeqNo cursor = 0;
    TimeNs lastReceivedNs = 0;
    SeqNo lastReceivedSeqNo = -1;


    Strat(OE& oe, L2OB& ob, XDPIO& io) : orderEntry{oe},
                                         ob{ob},
                                         io{io} {
    }

    template<Side side, bool IsReal = true>
    void __attribute__((always_inline))
    checkTrade(SeqNo seqNo, TimeNs mdTime, bool isSnapshot, TimeNs localTimestamp,
               PriceL price,
               Qty qty) {
        TopLevel t;
        CLOCK(BOOK_UPDATE_PC,
              t = ob.update<IsReal>(isSnapshot, side, localTimestamp, price, qty);
        )
        const auto& [bestBid, bestAsk, bidSize, askSize] = t;
        const SeqNo triggerEvent = seqNo;
        const TimeNs recvTime = mdTime;
        constexpr Qty tradeQty = 1;
        constexpr bool isBid = side == BUY;

        if constexpr (!IsReal) {
            const OrderFlags flags{.isBid = !isBid};
            orderEntry.submit<IsReal>(triggerEvent, recvTime, price, qty, flags);
            return;
        }

        if (__builtin_expect(!isSnapshot, true)) {
            PriceL sidePrice;
            PriceL oppSidePrice;
            Qty sideQty;

            if constexpr (side == BUY) {
                sidePrice = bestBid;
                sideQty = bidSize;
                oppSidePrice = bestAsk;
            } else if constexpr (side == SELL) {
                sidePrice = bestAsk;
                sideQty = askSize;
                oppSidePrice = bestBid;
            } else {
                throw std::runtime_error("Unexpected");
            }

            const PriceL notionalChange = price * qty - sidePrice * sideQty;
            if (__builtin_expect(bidSize != -1 && askSize != -1, true)) {
                if (notionalChange > TRADE_THRESHOLD) {
                    const PriceL tradePrice = oppSidePrice;
                    const OrderFlags flags{.isBid = !isBid};
                    orderEntry.submit<true>(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                } else if (notionalChange < -TRADE_THRESHOLD) {
                    const PriceL tradePrice = sidePrice;
                    const OrderFlags flags{.isBid = isBid};
                    orderEntry.submit<true>(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                }
            }
        }
    }

    template<bool IsReal = true>
    const u8* handleMessages(const u8* inBuf, u64 numPackets, TimeNs time) {
        assert(!isComplete);
        assert(inBuf != nullptr);
        assert(numPackets > 0);
        assert(time > 0);
        assert(time > lastReceivedNs);

        static int counter = 0;
        ++counter;

        UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        const u32 ogLowestSeqNum = udpBuf.nextMissingSeqNo;
        const u32 ogHead = udpBuf.head;

        const u8* finalBufPos = inBuf;
        for (int i = 0; i < numPackets; ++i) {
            const MDPayload& packet = *reinterpret_cast<const MDPayload *>(finalBufPos);
            const TimeNs timeDelay = currentTimeNs() - packet.localTimestamp;
            // assert(timeDelay <= 5'000'000'000);
            assert(packet.packetType == MD_PACKET_TYPE);
            if (__builtin_expect(!packet.flags.isTerm, true)) {
                assert(packet.seqNo >= 0 || packet.flags.isTerm);
                assert(packet.price > 0);
                assert(packet.qty >= 0);
                if constexpr (IsReal) {
                    const int processed = udpBuf.newMessage<true>(time, packet, *this);
                    assert(processed > 0);
                } else {
                    const int processed = udpBuf.newMessage<false>(time, packet, *this);
                    assert(processed == 0);
                }
            } else {
                assert(i == numPackets - 1);
                isComplete = true;
            }
            finalBufPos += sizeof(MDPayload);
        }

        assert(udpBuf.nextMissingSeqNo >= ogLowestSeqNum);
        assert(udpBuf.nextMissingSeqNo == ogLowestSeqNum || udpBuf.head != ogHead);
        assert(finalBufPos - inBuf == numPackets * sizeof(MDPayload));

        return finalBufPos;
    }

    template<bool IsReal>
    void processPacket(TimeNs time, const MDPayload& p) {
        assert(!isComplete);
        isComplete = p.flags.isTerm;


        if (__builtin_expect(!isComplete, true)) {
            assert(p.seqNo > -1);
            assert(p.seqNo > lastReceivedSeqNo);
            assert(p.price > 0);
            assert(p.qty >= 0);

            const SeqNo seqNo = p.seqNo;
            const TimeNs timestamp = p.localTimestamp;
            const TimeNs localTimestamp = timestamp;
            const PriceL price = p.price;
            const Qty qty = p.qty;
            const bool isSnapshot = p.flags.isSnapshot;

            if constexpr (!IsReal) {
                checkTrade<BUY, false>(lastReceivedSeqNo + 1, time, false, time, price, qty);
                return;
            }

            if (qty > 0) {
                static bool isSnapshotting = false;
                if (!isSnapshotting && isSnapshot) {
                    ob.clear();
                }
                isSnapshotting = isSnapshot;
                if (p.flags.isBid) {
                    checkTrade<BUY, IsReal>(seqNo, time, isSnapshot, localTimestamp, price, qty);
                } else {
                    checkTrade<SELL, IsReal>(seqNo, time, isSnapshot, localTimestamp, price, qty);
                }
            } else {
                CLOCK(BOOK_UPDATE_PC,
                      ob.cancel(localTimestamp, price, p.flags.isBid ? BUY : SELL);
                )
            }

            cursor = p.seqNo;
            lastReceivedSeqNo = p.seqNo;
        }
    }

    template<bool IsReal>
    ErrorCode recvUdpMD(const u8* frameBuffer, u32 len) {
        assert(!udpBuf.test(0));
        assert(orderEntry.isConnected());
        assert(frameBuffer != nullptr);

        const UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        const SeqNo ogSeqNo = udpBuf.nextMissingSeqNo;
        const SeqNo ogCursor = cursor;

        const bool isAlive = isConnected();

        if (__builtin_expect(isAlive, true)) {
            const auto curTime = currentTimeNs();

            const i64 bytesReadWithPhy = len;

            assert(bytesReadWithPhy > 0);
            assert(bytesReadWithPhy <= READ_BUF_SZ);

            assert((reinterpret_cast<u64>(frameBuffer) & 127) == 0);
            const auto* packet = reinterpret_cast<const MDFrame *>(frameBuffer);
            assert(packet->eth.h_proto == htons(ETH_P_IP));
            assert(packet->ip.version == 4);
            assert(packet->ip.ihl == 5);
            assert(htons(packet->ip.tot_len) == bytesReadWithPhy - sizeof(ethhdr));
            assert((htons(packet->ip.tot_len) - sizeof(iphdr) - sizeof(udphdr)) % sizeof(MDPayload) == 0);
            assert((ntohs(packet->ip.frag_off) & 0x1fff) == 0);
            assert(((packet->ip.frag_off >> 13) & 1) == 0);
            assert(((packet->ip.frag_off >> 15) & 1) == 0);
            assert(htons(packet->udp.len) == htons(packet->ip.tot_len) - sizeof(iphdr));

            const u8* dataBuf = frameBuffer + sizeof(MDFrame);
            u32 bytesRead = bytesReadWithPhy - sizeof(MDFrame);

            const u64 numPackets = bytesRead / sizeof(MDPayload);
            assert(bytesRead % sizeof(MDPayload) == 0);

            if constexpr (IsReal) {
                assert(checkMessageDigest(dataBuf, bytesRead));
            }
            const u8* endBuf = handleMessages<IsReal>(dataBuf, numPackets, curTime);
            assert(endBuf == dataBuf + sizeof(MDPayload) * numPackets);


            const TimeNs now = currentTimeNs();
            // assert(lastReceivedNs == 0 || now - lastReceivedNs < 100'000'000);
            lastReceivedNs = now;


            assert(!udpBuf.test(0));
            assert(ogSeqNo == udpBuf.nextMissingSeqNo || cursor > ogCursor && (ogMask == 0 || ogMask != udpBuf.mask));
            assert(cursor >= ogCursor);
        }
        // TODO - translate some assertions to error codes
        return {};
    }

    [[nodiscard]] bool isConnected() const noexcept {
        TimeNs now = currentTimeNs();
        //        bool notDelayed = std::abs(now - lastReceivedNs) < 10'000'000;
        //        return !isComplete && mdFD != -1 && (lastReceivedNs == 0 || notDelayed);
        return !isComplete;
    }
};

#endif //LLL_EXCHANGE_STRAT_H
