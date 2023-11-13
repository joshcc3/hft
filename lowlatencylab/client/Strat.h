//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_STRAT_H
#define LLL_EXCHANGE_STRAT_H


#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <sched.h>
#include <new>
#include <cassert>
#include <bitset>
#include <memory>
#include <x86intrin.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <array>
#include "OE.h"
#include "../defs.h"
#include "L2OB.h"
#include "mdmcclient.h"
#include "UDPBuffer.h"

class Strat : public PacketProcessor<Strat> {
public:

    static constexpr u32 READ_BUF_SZ = 1 << 12;

    bool isComplete = false;

    L2OB &ob;
    OE &orderEntry;

    std::unique_ptr<u8[]> readBuf;
    UDPBuffer<PacketProcessor<Strat>> udpBuf{};
    int mdFD = -1;
    SeqNo cursor = 0;
    TimeNs lastReceivedNs = 0;
    SeqNo lastReceivedSeqNo = -1;


    Strat(OE &oe, L2OB &ob) : readBuf{std::make_unique<u8[]>(READ_BUF_SZ)},
                              orderEntry{oe},
                              ob{ob} {

        mdFD = socket(AF_INET, SOCK_DGRAM, 0);
        if (mdFD < 0) {
            perror("Strat socket");
            exit(EXIT_FAILURE);
        }

        int enable = 1;
        if (setsockopt(mdFD, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
            perror("MD Reusing ADDR failed");
            close(mdFD);
            exit(EXIT_FAILURE);
        }

        if (setsockopt(mdFD, SOL_SOCKET, SO_INCOMING_CPU, &PINNED_CPU, sizeof(PINNED_CPU)) < 0) {
            perror("MD Cpu Affinity failed.");
            close(mdFD);
            exit(EXIT_FAILURE);
        }

        ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR.c_str());
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(mdFD, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            perror("Membership join");
            close(mdFD);
            exit(EXIT_FAILURE);
        }


        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY); // inet_addr("127.0.0.1"); // Receive from any address
        addr.sin_port = htons(MCAST_PORT);

        // Bind to receive address
        if (bind(mdFD, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            perror("MD bind");
            close(mdFD);
            exit(EXIT_FAILURE);
        }


    }

    ~Strat() {
        if (mdFD != -1) {
            if (close(mdFD) == -1) {
                perror("Failed to locse md");
            }
            mdFD = -1;
        }
    }

    template<Side side>
    void __attribute__((always_inline))
    checkTrade(SeqNo seqNo, TimeNs mdTime, bool isSnapshot, TimeNs localTimestamp,
               PriceL price,
               Qty qty) {
        const auto &[bestBid, bestAsk, bidSize, askSize] = ob.update(isSnapshot, side, localTimestamp, price, qty);
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
                static_assert(false);
            }

            PriceL notionalChange = price * qty - sidePrice * sideQty;

            if (bidSize != -1 && askSize != -1) {
                if (notionalChange > TRADE_THRESHOLD) {
                    PriceL tradePrice = oppSidePrice;
                    OrderFlags flags{.isBid = !isBid};
                    orderEntry.submit(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                } else if (notionalChange < -TRADE_THRESHOLD) {
                    PriceL tradePrice = sidePrice;
                    OrderFlags flags{.isBid = isBid};
                    orderEntry.submit(triggerEvent, recvTime, tradePrice, tradeQty, flags);
                }
            }
        }
    }

    const u8 *handleMessages(const u8 *inBuf, u64 numPackets, TimeNs time) {
        assert(!isComplete);
        assert(inBuf != nullptr);
        assert(numPackets > 0);
        assert(time > 0);
        assert(time > lastReceivedNs);
        assert(mdFD != -1);

        UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        u32 ogLowestSeqNum = udpBuf.nextMissingSeqNo;
        u32 ogHead = udpBuf.head;
        OrderId ogCurOrder = orderEntry.curOrder.id;

        const u8 *finalBufPos = inBuf;

        for (int i = 0; i < numPackets; ++i) {

            const MDPacket &packet = *reinterpret_cast<const MDPacket *>(finalBufPos);
            cout << packet.seqNo << '\n';
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

    void processPacket(TimeNs time, const MDPacket &p) {
        assert(!isComplete);
        isComplete = p.flags.isTerm;
        if (!isComplete) {
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
                ob.cancel(localTimestamp, price, p.flags.isBid ? BUY : SELL);
            }

            cursor = p.seqNo;
            lastReceivedSeqNo = p.seqNo;
        }
    }

    void recvUdpMD() {
        auto curTime = currentTimeNs();

        assert(mdFD > 2);
        assert(!udpBuf.test(0));
        assert(orderEntry.isConnected());

        UDPBuffer<Strat>::MaskType ogMask = udpBuf.mask;
        SeqNo ogSeqNo = udpBuf.nextMissingSeqNo;
        SeqNo ogCursor = cursor;

        bool isAlive = isConnected();

        if (isAlive) {

            ssize_t bytesRead = recv(mdFD, readBuf.get(), READ_BUF_SZ, MSG_TRUNC);

            assert(bytesRead > 0);
            assert(bytesRead <= READ_BUF_SZ);

            u64 numPackets = bytesRead / sizeof(MDPacket);
            assert(bytesRead % sizeof(MDPacket) == 0);
            u8 *const inBuf = readBuf.get();

            assert(checkMessageDigest(inBuf, bytesRead));

            const u8 *endBuf = handleMessages(inBuf, numPackets, curTime);
            assert(endBuf == inBuf + sizeof(MDPacket) * numPackets);
        }

        TimeNs now = currentTimeNs();
        assert(lastReceivedNs == 0 || now - lastReceivedNs < 5'000'000'000);
        lastReceivedNs = now;

        assert(!udpBuf.test(0));
        assert(ogSeqNo == udpBuf.nextMissingSeqNo || cursor > ogCursor && (ogMask == 0 || ogMask != udpBuf.mask));
        assert(cursor >= ogCursor);
        assert(lastReceivedNs == now);

    }

    [[nodiscard]] bool isConnected() const noexcept {
        TimeNs now = currentTimeNs();
        bool notDelayed = std::abs(now - lastReceivedNs) < 1000000;
        return !isComplete && mdFD != -1 && notDelayed;
    }
};

#endif //LLL_EXCHANGE_STRAT_H
