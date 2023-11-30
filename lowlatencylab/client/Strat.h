//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_STRAT_H
#define LLL_EXCHANGE_STRAT_H


#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <sched.h>
#include <new>
#include <cassert>
#include <netinet/in.h>
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

    UDPBuffer<PacketProcessor<Strat>> udpBuf{};
    SeqNo cursor = 0;
    TimeNs lastReceivedNs = 0;
    SeqNo lastReceivedSeqNo = -1;


    Strat(OE& oe, L2OB& ob, XDPIO& io) : orderEntry{oe},
                                         ob{ob},
                                         io{io} {
    }

    template<Side side>
    void __attribute__((always_inline))
    checkTrade(SeqNo seqNo, TimeNs mdTime, bool isSnapshot, TimeNs localTimestamp,
               PriceL price,
               Qty qty) {
        const auto& [bestBid, bestAsk, bidSize, askSize] = ob.update(isSnapshot, side, localTimestamp, price, qty);
        if (!isSnapshot) {
            SeqNo triggerEvent = seqNo;
            TimeNs recvTime = mdTime;
            Qty tradeQty = 1;

            PriceL sidePrice;
            PriceL oppSidePrice;
            Qty sideQty;
            constexpr bool isBid = side == BUY;

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

            PriceL notionalChange = price * qty - sidePrice * sideQty;

            if (bidSize != -1 && askSize != -1) {
                if (notionalChange > TRADE_THRESHOLD) {
                    PriceL tradePrice = oppSidePrice;
                    OrderFlags flags{.isBid = !isBid};
                    CLOCK(ORDER_SUBMISSION_PC,
                          orderEntry.submit(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                    )
                } else if (notionalChange < -TRADE_THRESHOLD) {
                    PriceL tradePrice = sidePrice;
                    OrderFlags flags{.isBid = isBid};
                    CLOCK(ORDER_SUBMISSION_PC,
                          orderEntry.submit(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                    )
                }
            }
        }
    }

    const u8* handleMessages(const u8* inBuf, u64 numPackets, TimeNs time) {
        assert(!isComplete);
        assert(inBuf != nullptr);
        assert(numPackets > 0);
        assert(time > 0);
        assert(time > lastReceivedNs);

        UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        u32 ogLowestSeqNum = udpBuf.nextMissingSeqNo;
        u32 ogHead = udpBuf.head;
        OrderId ogCurOrder = orderEntry.curOrder.id;

        const u8* finalBufPos = inBuf;

        for (int i = 0; i < numPackets; ++i) {
            const MDPacket& packet = *reinterpret_cast<const MDPacket *>(finalBufPos);
            TimeNs timeDelay = currentTimeNs() - packet.localTimestamp;
            assert(timeDelay <= 5'000'000'000);
            assert(packet.seqNo >= 0 || packet.flags.isTerm);
            assert(packet.price > 0);
            assert(packet.qty >= 0);
            int processed = udpBuf.newMessage(time, packet, *this);
            assert(orderEntry.curOrder.id == ogCurOrder || processed > 0);
            finalBufPos += sizeof(MDPacket);
        }

        assert(udpBuf.nextMissingSeqNo >= ogLowestSeqNum);
        assert(udpBuf.nextMissingSeqNo == ogLowestSeqNum || udpBuf.head != ogHead);
        assert(finalBufPos - inBuf == numPackets * sizeof(MDPacket));

        return finalBufPos;
    }

    void processPacket(TimeNs time, const MDPacket& p) {
        assert(!isComplete);
        isComplete = p.flags.isTerm;
        if (__builtin_expect(!isComplete, true)) {
            assert(p.seqNo > -1);
            assert(p.seqNo > lastReceivedSeqNo);
            assert(p.price > 0);
            assert(p.qty >= 0);

            SeqNo seqNo = p.seqNo;
            TimeNs timestamp = p.localTimestamp;
            TimeNs localTimestamp = timestamp;
            PriceL price = p.price;
            Qty qty = p.qty;
            bool isSnapshot = p.flags.isSnapshot;

            if (qty > 0) {
                static bool isSnapshotting = false;
                if (!isSnapshotting && isSnapshot) {
                    ob.clear();
                }
                isSnapshotting = isSnapshot;
                if (p.flags.isBid) {
                    checkTrade<Side::BUY>(seqNo, time, isSnapshot, localTimestamp, price, qty);
                } else {
                    checkTrade<Side::SELL>(seqNo, time, isSnapshot, localTimestamp, price, qty);
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

    void recvUdpMD() {
        auto curTime = currentTimeNs();

        assert(!udpBuf.test(0));
        assert(orderEntry.isConnected());

        UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        SeqNo ogSeqNo = udpBuf.nextMissingSeqNo;
        SeqNo ogCursor = cursor;

        bool isAlive = isConnected();

        if (__builtin_expect(isAlive, true)) {
            const auto& [inBuf, bytesRead, readDesc] = io.recvBlocking();
            assert(inBuf != nullptr);
            // CLOCK(SYS_RECV_PC,
            // )
            assert(bytesRead > 0);
            assert(bytesRead <= READ_BUF_SZ);

            u64 numPackets = bytesRead / sizeof(MDPacket);
            assert(bytesRead % sizeof(MDPacket) == 0);

            assert(checkMessageDigest(inBuf, bytesRead));

            const u8* endBuf = handleMessages(inBuf, numPackets, curTime);
            assert(endBuf == inBuf + sizeof(MDPacket) * numPackets);


            TimeNs now = currentTimeNs();
            assert(lastReceivedNs == 0 || now - lastReceivedNs < 100'000'000);
            lastReceivedNs = now;
            io.completeRead(readDesc);
        }


        assert(!udpBuf.test(0));
        assert(ogSeqNo == udpBuf.nextMissingSeqNo || cursor > ogCursor && (ogMask == 0 || ogMask != udpBuf.mask));
        assert(cursor >= ogCursor);
    }

    [[nodiscard]] bool isConnected() const noexcept {
        TimeNs now = currentTimeNs();
        //        bool notDelayed = std::abs(now - lastReceivedNs) < 10'000'000;
        //        return !isComplete && mdFD != -1 && (lastReceivedNs == 0 || notDelayed);
        return !isComplete;
    }
};

#endif //LLL_EXCHANGE_STRAT_H
