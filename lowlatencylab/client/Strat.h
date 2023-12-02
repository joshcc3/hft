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

    template<Side side>
    void __attribute__((always_inline))
    checkTrade(SeqNo seqNo, TimeNs mdTime, bool isSnapshot, TimeNs localTimestamp,
               PriceL price,
               Qty qty) {
        const auto& [bestBid, bestAsk, bidSize, askSize] = ob.update(isSnapshot, side, localTimestamp, price, qty);
        if (__builtin_expect(!isSnapshot, true)) {
            const SeqNo triggerEvent = seqNo;
            const TimeNs recvTime = mdTime;
            const Qty tradeQty = 1;

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

            const PriceL notionalChange = price * qty - sidePrice * sideQty;
            if (__builtin_expect(bidSize != -1 && askSize != -1, true)) {
                if (notionalChange > TRADE_THRESHOLD) {
                    const PriceL tradePrice = oppSidePrice;
                    const OrderFlags flags{.isBid = !isBid};
                    orderEntry.submit(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                } else if (notionalChange < -TRADE_THRESHOLD) {
                    const PriceL tradePrice = sidePrice;
                    const OrderFlags flags{.isBid = isBid};
                    orderEntry.submit(triggerEvent, recvTime, tradePrice, tradeQty, flags);
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

        static int counter = 0;
        ++counter;

        UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        const u32 ogLowestSeqNum = udpBuf.nextMissingSeqNo;
        const u32 ogHead = udpBuf.head;

        const u8* finalBufPos = inBuf;

        for (int i = 0; i < numPackets; ++i) {
            const MDPacket& packet = *reinterpret_cast<const MDPacket *>(finalBufPos);
            const TimeNs timeDelay = currentTimeNs() - packet.localTimestamp;
            // assert(timeDelay <= 5'000'000'000);
            assert(packet.packetType == MD_PACKET_TYPE);
            if(__builtin_expect(!packet.flags.isTerm, true)) {
                assert(packet.seqNo >= 0 || packet.flags.isTerm);
                assert(packet.price > 0);
                assert(packet.qty >= 0);
                const int processed = udpBuf.newMessage(time, packet, *this);
                assert(processed > 0);
            } else {
                assert(i == numPackets - 1);
                isComplete = true;
            }
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

            const SeqNo seqNo = p.seqNo;
            const TimeNs timestamp = p.localTimestamp;
            const TimeNs localTimestamp = timestamp;
            const PriceL price = p.price;
            const Qty qty = p.qty;
            const bool isSnapshot = p.flags.isSnapshot;

            if (qty > 0) {
                static bool isSnapshotting = false;
                if (!isSnapshotting && isSnapshot) {
                    ob.clear();
                }
                isSnapshotting = isSnapshot;
                if (p.flags.isBid) {
                    checkTrade<BUY>(seqNo, time, isSnapshot, localTimestamp, price, qty);
                } else {
                    checkTrade<SELL>(seqNo, time, isSnapshot, localTimestamp, price, qty);
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
        assert(!udpBuf.test(0));
        assert(orderEntry.isConnected());

        const UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        const SeqNo ogSeqNo = udpBuf.nextMissingSeqNo;
        const SeqNo ogCursor = cursor;

        const bool isAlive = isConnected();

        if (__builtin_expect(isAlive, true)) {
            io.qs.stateCheck();
            io.umem.stateCheck();
            io.socket.stateCheck();

            u32 idx;
            u32 available;
            u32 fillQIdx;
            u32 reserved;
            CLOCK(SYS_RECV_PC,
                  // if we get multiple out of order packets then we should process them smartly - last to first.
                  const auto& res = io.blockRecv();
                  available = std::get<0>(res);
                  reserved = std::get<1>(res);
                  idx = std::get<2>(res);
                  fillQIdx = std::get<3>(res);
            )
            assert(reserved == available);

            for (int i = 0; i < available; ++i) {
                const auto curTime = currentTimeNs();

                const xdp_desc* readDesc = xsk_ring_cons__rx_desc(&io.qs.rxQ, idx + i);
                const __u64 addr = readDesc->addr;
                const __u32 len = readDesc->len;
                const __u32 options = readDesc->options;

                assert(xsk_umem__extract_addr(addr) == addr);
                assert(xsk_umem__extract_offset(addr) == 0);
                assert(options == 0);
                assert((u64(io.umem.buffer) & 4095) == 0);
                assert((addr & 255) == 0 && ( addr - 256 & XSKUmem_FRAME_SIZE - 1) == 0);
                assert(addr < XSKUmem_FRAME_SIZE * (io.umem.NUM_FRAMES - 1));
                assert(len < XSKUmem_FRAME_SIZE);

                u8* readAddr = static_cast<u8 *>(xsk_umem__get_data(io.umem.buffer, addr));

                assert(readAddr == io.umem.buffer + addr);


                const u8* inBuf = readAddr;
                u32 bytesReadWithPhy = len;
                assert(inBuf != nullptr);

                assert(bytesReadWithPhy > 0);
                assert(bytesReadWithPhy <= READ_BUF_SZ);

                assert((reinterpret_cast<u64>(inBuf) & 127) == 0);
                const MDFrame* packet = reinterpret_cast<const MDFrame *>(inBuf);
                assert(packet->eth.h_proto == htons(ETH_P_IP));
                assert(packet->ip.version == 4);
                assert(packet->ip.ihl == 5);
                assert(htons(packet->ip.tot_len) == bytesReadWithPhy - sizeof(ethhdr));
                assert((htons(packet->ip.tot_len) - sizeof(iphdr) - sizeof(udphdr)) % sizeof(MDPacket) == 0);
                //assert((packet->ip.frag_off & 0x1fff) == 0);
                assert(((packet->ip.frag_off >> 13) & 1) == 0);
                assert(((packet->ip.frag_off >> 15) & 1) == 0);
                assert(htons(packet->udp.len) == htons(packet->ip.tot_len) - sizeof(iphdr));

                const u8* dataBuf = inBuf + sizeof(MDFrame);
                u32 bytesRead = bytesReadWithPhy - sizeof(MDFrame);

                const u64 numPackets = bytesRead / sizeof(MDPacket);
                assert(bytesRead % sizeof(MDPacket) == 0);

                assert(checkMessageDigest(dataBuf, bytesRead));

                const u8* endBuf = handleMessages(dataBuf, numPackets, curTime);
                assert(endBuf == dataBuf + sizeof(MDPacket) * numPackets);


                const TimeNs now = currentTimeNs();
                // assert(lastReceivedNs == 0 || now - lastReceivedNs < 100'000'000);
                lastReceivedNs = now;

                unsigned long long* fillQEntry = xsk_ring_prod__fill_addr(&io.qs.fillQ, fillQIdx + i);
                assert(fillQEntry != nullptr);
                *fillQEntry = readDesc->addr;
            }

            assert(!xsk_ring_prod__needs_wakeup(&io.qs.fillQ));
            xsk_ring_cons__release(&io.qs.rxQ, available);
            xsk_ring_prod__submit(&io.qs.fillQ, available);

            assert(!udpBuf.test(0));
            assert(ogSeqNo == udpBuf.nextMissingSeqNo || cursor > ogCursor && (ogMask == 0 || ogMask != udpBuf.mask));
            assert(cursor >= ogCursor);
        }
    }

    [[nodiscard]] bool isConnected() const noexcept {
        TimeNs now = currentTimeNs();
        //        bool notDelayed = std::abs(now - lastReceivedNs) < 10'000'000;
        //        return !isComplete && mdFD != -1 && (lastReceivedNs == 0 || notDelayed);
        return !isComplete;
    }
};

#endif //LLL_EXCHANGE_STRAT_H
