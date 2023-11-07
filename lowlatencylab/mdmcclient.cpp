//
// Created by jc on 04/11/23.
//

#include "mdmcclient.h"
#include "L2OB.h"
#include "defs.h"
#include <array>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <x86intrin.h>
#include <memory>
#include <bitset>
#include <cassert>
#include <new>
#include <sched.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>


enum class StrategyState {
    INIT,
    OE_CONNECT,
    MD_CONNECT,
    RUNNING
};

constexpr static int PINNED_CPU = 0;

// TODO - try and use oob data to send quick packets and see how much of a difference it makes for the exchange to receive it
class OE {
public:
    // TODO Use POLLHUP to determine when the other end has hung up

    constexpr static u64 ORDER_TAG = 3;

    IOUringState &ioState;
    int clientFD = -1;
    OrderId orderId = 1;

    sockaddr_in serverAddr{};

    static constexpr size_t msgSize = sizeof(Order);
    char outputBuf[msgSize]{};
    Order curOrder;

    explicit OE(IOUringState &ioState) : ioState{ioState} {}

    ~OE() {
        if (clientFD != -1) {
            if (close(clientFD) == -1) {
                perror("Failed to close OE socket");
            }
            clientFD = -1;

        }
    }

    void establishConnection() {
        assert(clientFD == -1);
        assert(orderId == 1);
        assert(io_uring_sq_ready(&ioState.ring) == 0);
        assert(io_uring_cq_ready(&ioState.ring) == 0);

        clientFD = socket(AF_INET, SOCK_STREAM, 0);
        if (clientFD == -1) {
            cerr << "Could not create oe server [" << errno << "]" << endl;
            exit(EXIT_FAILURE);
        }
        assert(clientFD == 5); // assume that this is the second socket opened

        int enable = 1;
        if (setsockopt(clientFD, SOL_SOCKET, SO_DONTROUTE, &enable, sizeof(enable)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(SO_DONTROUTE) failed");
        }
        enable = 1;
        if (setsockopt(clientFD, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(SO_KEEPALIVE) failed");
        }

        int minBytes = 1;
        if (setsockopt(clientFD, SOL_SOCKET, SO_RCVLOWAT, &minBytes, sizeof(enable)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(SO_RCVLOWAT) failed");
        }

        // TODO - set SO_PRIORITY
        int tos = IPTOS_LOWDELAY; // The TOS value for low delay
        if (setsockopt(clientFD, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
            std::cerr << "Error setting TOS" << std::endl;
            close(clientFD); // Always clean up sockets when done
            throw std::runtime_error("setsockopt(IP_TOS) failed");
        }

        int initialProbe = 10;
        if (setsockopt(clientFD, IPPROTO_TCP, TCP_KEEPIDLE, &initialProbe, sizeof(initialProbe)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(TCP_KEEPIDLE) failed");
        }
        int subsequentProbes = 2;
        if (setsockopt(clientFD, IPPROTO_TCP, TCP_KEEPINTVL, &subsequentProbes, sizeof(subsequentProbes)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(TCP_KEEPINTVL) failed");
        }
        int maxProbes = 2;
        if (setsockopt(clientFD, IPPROTO_TCP, TCP_KEEPCNT, &maxProbes, sizeof(maxProbes)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(TCP_KEEPCNT) failed");
        }
        enable = 1;
        if (setsockopt(clientFD, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) < 0) {
            close(clientFD);
            throw std::runtime_error("setsockopt(TCP_NODELAY) failed");
        }


        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        serverAddr.sin_port = htons(OE_PORT);

        if (int res = connect(clientFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr))) {
            perror("Bind to server socket failed.");
            cerr <<
            close(clientFD);
            exit(EXIT_FAILURE);
        }

    }

    void submit(MDMsgId triggerEvent, TimeNs triggerRecvTime, PriceL price, Qty qty, OrderFlags flags) {
/*
    TODO - setup queue polling to avoid the system call for the kernel to automatically
    pick up submits.
*/

        assert(clientFD != -1);
        assert(io_uring_sq_space_left(&ioState.ring) > 1);
        assert(io_uring_sq_ready(&ioState.ring) == 0);

        TimeNs submitTime = currentTimeNs();


        Order &o = *reinterpret_cast<Order *>(outputBuf);
        o.submittedTime = submitTime;
        o.triggerEvent = triggerEvent;
        o.triggerReceivedTime = triggerRecvTime;
        o.id = orderId++;
        o.price = price;
        o.qty = qty;
        o.flags = flags;

        assert(o.submittedTime == submitTime);
        assert(o.triggerEvent == triggerEvent);
        assert(o.triggerReceivedTime == triggerRecvTime);
        assert(o.id == orderId);
        assert(o.price == price);
        assert(o.qty == qty);

        io_uring_sqe *submitSqe = ioState.getSqe(o.id + ORDER_TAG);
        int sendFlags = MSG_DONTROUTE | MSG_DONTWAIT;
        io_uring_prep_send(submitSqe, clientFD, static_cast<void *>(outputBuf), msgSize, sendFlags);
        assert(submitSqe->flags == 0);

        assert(io_uring_sq_ready(&ioState.ring) == 0);

        int submits = io_uring_submit(&ioState.ring);
        assert(submits == 1);

        curOrder = o;

        assert(isConnected());


    }

    [[nodiscard]] bool isConnected() const noexcept {
        if (clientFD != -1) {
            if (read(clientFD, nullptr, 0) == -1) {
                perror("OE Connection invalid");
                return false;
            } else {
                return true;
            }

        } else {
            return false;
        }
    }

    void completeMessage(io_uring_cqe &completion) {
        auto curTime = currentTimeNs();

        assert(io_uring_cq_ready(&ioState.ring) >= 1);
        assert(ioState.ring.cq.cqes == &completion);
        assert(clientFD > 2);
        assert(orderId > 1);
        assert(curOrder.id >= 1);

        i32 cRes = completion.res;
        u32 cFlags = completion.flags;
        u64 cUserData = io_uring_cqe_get_data64(&completion);

        OrderId receivedId = cUserData - ORDER_TAG;

        assert(receivedId == curOrder.id);
        assert(cRes > 0);
        assert(!(cFlags & IORING_CQE_F_BUFFER));
        assert(!(cFlags & IORING_CQE_F_NOTIF));
        assert(cRes > 0 || -cRes != EBADF);

        cout << "Order Id [" << curOrder.id << "]." << endl;
        cout << "Order Latency Time [" << curTime - curOrder.triggerReceivedTime << "]." << endl;
        cout << "Order Ack Time [" << curTime - curOrder.submittedTime << "]." << endl;

    }
};

struct UDPBuffer {
    constexpr static int Sz = 32;
    static_assert(__builtin_popcount(Sz) == 1);
    static_assert(Sz == 32);

    using BufferItem = MDPacket;
    u32 mask = 0;
    SeqNo lowestSeqNum = 0;
    u32 head = 0;
    std::array<BufferItem, Sz> ringBuffer{};

    const MDPacket &get(int i) {
        assert(i >= 0 && i < Sz);
        u32 bufferIx = (head + i) & (Sz - 1);
        assert(mask >> bufferIx);
        return ringBuffer[bufferIx];
    }

    [[nodiscard]] bool test(int pos) const {
        assert(pos < Sz && pos >= 0);
        return mask >> (1 << ((head + pos) & (Sz - 1)));
    }

    int newMessage(SeqNo seqNo, const MDPacket &msg) {
        assert(head < Sz);
        assert(lowestSeqNum >= 0);
        assert((mask & u32(-1)) != u32(-1));
        assert(seqNo >= lowestSeqNum);
        u32 bufferOffs = seqNo - lowestSeqNum;
        assert(bufferOffs < Sz);
        u32 bufferPos = (head + bufferOffs) & (Sz - 1);
        int maskBit = 1 << bufferPos;
        assert((mask & maskBit) == 0);
        for (int i = 0; i < Sz; ++i) {
            if (test(i)) {
                assert(ringBuffer[i].seqNo != -1);
            }
        }
        mask |= maskBit;
        u32 alignedMask = _rotl(mask, head);
        assert(_rotr(alignedMask, head) == mask);
        u32 fullMask = ((u64(1) << (bufferOffs + 1)) - 1) << (Sz - bufferOffs - 1);
        bool isFull = (fullMask & alignedMask) == fullMask;
        new(&ringBuffer[bufferPos]) BufferItem{msg};
        assert(ringBuffer[bufferPos].seqNo == msg.seqNo);
        assert(ringBuffer[bufferPos].price == msg.price);
        assert(ringBuffer[bufferPos].localTimestamp == msg.localTimestamp);
        return isFull ? i32(bufferOffs + 1) : 0;
    }

    void advance(int n) {
        u32 alignedMask = _rotl(mask, head);
        assert(_rotr(alignedMask, head) == mask);
        u32 fullMask = ((u64(1) << n) - 1) << (Sz - n);
        bool isFull = (fullMask & alignedMask) == fullMask;
        assert(isFull);
        head = (head + n) & (Sz - 1);
    }
};

struct ReceiveHeader {
    static constexpr int GROUP_ID = 2;
    static constexpr int NUM_BUFFERS = 1 << 5;
    constexpr static int NAME_LEN = 64;
    constexpr static int CONTROL_LEN = 0;
    static constexpr int BUFFER_SIZE = 1 << 9;
    constexpr static int BUFFER_TAG = 0;
    constexpr static int RECV_TAG = 1;

    msghdr hdr{};
    char sourceAddr[NAME_LEN]{};
    iovec vecs[NUM_BUFFERS]{};
    char controlData[CONTROL_LEN];

    std::unique_ptr<u8[]> buffers;
    std::bitset<ReceiveHeader::NUM_BUFFERS> used;


    ReceiveHeader(): buffers{std::make_unique<u8[]>(BUFFER_SIZE * ReceiveHeader::NUM_BUFFERS)},
                     used{} {

        for(int i = 0; i < NUM_BUFFERS; ++i) {
            vecs[i].iov_base = buffers.get() + i * BUFFER_SIZE;
            vecs[i].iov_len = BUFFER_SIZE;
        }

        hdr.msg_name = sourceAddr;
        hdr.msg_namelen = NAME_LEN;
        hdr.msg_control = controlData;
        hdr.msg_controllen = CONTROL_LEN;
        hdr.msg_iov = vecs;
        hdr.msg_iovlen = NUM_BUFFERS;
        hdr.msg_flags = 0;
    }
    void prepareRecv(IOUringState& ioState, int mdFD) {

        assert(used.all() || !used.any());
        used.reset();

        io_uring_sqe *bufferSqe = ioState.getSqe(BUFFER_TAG);
        io_uring_prep_provide_buffers(bufferSqe, buffers.get(), BUFFER_SIZE, ReceiveHeader::NUM_BUFFERS, GROUP_ID, 0);
        assert(io_uring_sq_ready(&ioState.ring) == 1);
        int completed = io_uring_submit_and_wait(&ioState.ring, 1);
        assert(completed == 1);
        assert(io_uring_sq_ready(&ioState.ring) == 0);
        {
            cqe_guard g{ioState};
            assert(g.completion->res == ReceiveHeader::NUM_BUFFERS || g.completion->res == 0);
            assert((io_uring_cqe_get_data64(g.completion)) == BUFFER_TAG);

        }

        assert(mdFD != -1);
        io_uring_sqe *recvSqe = ioState.getSqe(RECV_TAG);
        io_uring_prep_recvmsg_multishot(recvSqe, 0, &hdr,  MSG_TRUNC); // Use mdFD
        int ogFlags = recvSqe->flags;
        recvSqe->flags |= IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE;
        assert(recvSqe->flags == (ogFlags | IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE));

        assert(recvSqe->opcode == IORING_OP_RECVMSG);
        assert((recvSqe->fd == 0) == (recvSqe->flags & IOSQE_FIXED_FILE));
        assert(recvSqe->off == 0);
        assert(recvSqe->user_data == RECV_TAG);
    }

    u8* complete(io_uring_cqe& completion) {
        u32 bufferIx = completion.flags >> (sizeof(completion.flags) * 8 - 16);
        assert(bufferIx < ReceiveHeader::NUM_BUFFERS);
        assert(!used.test(bufferIx));

        u8 *buf = buffers.get() + bufferIx * BUFFER_SIZE;
        used.set(bufferIx);

        i32 datagramLen = completion.res;
        io_uring_recvmsg_out *out = io_uring_recvmsg_validate(static_cast<void *>(buf), datagramLen, &hdr);
        assert(nullptr != out);

        assert(!(out->flags & MSG_TRUNC));
        assert(!(out->flags & MSG_OOB));

        u32 receivedLen = io_uring_recvmsg_payload_length(out, datagramLen, &hdr);
        assert(receivedLen == datagramLen);

        u8 *payload = static_cast<u8 *>(io_uring_recvmsg_payload(out, &hdr));
        
        return payload;
    }


};

class Strat {
public:

    bool isComplete = false;

    L2OB &ob;
    OE &orderEntry;

    IOUringState &ioState;
    ReceiveHeader receiveHdr{};
    UDPBuffer udpBuf{};
    int mdFD = -1;
    bool multishotDone = true;
    SeqNo cursor = 0;
    TimeNs lastReceivedNs = -1;


    Strat(IOUringState &ioState, OE &oe, L2OB &ob) : ioState{ioState},
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

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Receive from any address
        addr.sin_port = htons(MCAST_PORT);

        // Bind to receive address
        if (bind(mdFD, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            perror("MD bind");
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
        const auto &[bestBid, bestAsk, bidSize, askSize] = ob.update<side>(ob.bid, ob.ask,
                                                                           localTimestamp, price, qty);

        if (!isSnapshot) {
            SeqNo triggerEvent = seqNo;
            TimeNs recvTime = mdTime;
            Qty tradeQty = 1;

            PriceL sidePrice;
            PriceL oppSidePrice;
            Qty sideQty;
            constexpr bool isBid = side == Side::BUY;

            if constexpr (side == Side::BUY) {
                sidePrice = bestBid;
                sideQty = bidSize;
                oppSidePrice = bestAsk;
            } else if constexpr (side == Side::SELL) {
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

    u8 *handleMessages(u8 *inBuf, u64 numPackets, TimeNs time) {
        assert(isComplete);
        assert(inBuf != nullptr);
        assert(numPackets > 0);
        assert(time > 0);
        assert(time > lastReceivedNs);
        assert(mdFD != -1);

        u32 ogMask = udpBuf.mask;
        u32 ogLowestSeqNum = udpBuf.lowestSeqNum;
        u32 ogHead = udpBuf.head;
        bool ogMultishotDone = multishotDone;
        std::bitset<ReceiveHeader::NUM_BUFFERS> ogUsed{receiveHdr.used};

        u8 *finalBufPos = inBuf;

        for (int i = 0; i < numPackets; ++i) {

            MDPacket &packet = *reinterpret_cast<MDPacket *>(finalBufPos);
            int ready = udpBuf.newMessage(packet.seqNo, packet);
            for (int j = 0; j < ready; ++j) {
                assert(!isComplete);

                const MDPacket &p = udpBuf.get(i);
                isComplete = p.flags.isTerm;
                if (!isComplete) {
                    assert(p.seqNo > -1);
                    assert(p.localTimestamp > lastReceivedNs);
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
                        if(!isSnapshotting && isSnapshot) {
                            ob.clear();
                        }
                        isSnapshotting = isSnapshot;
                        if (p.flags.isBid) {
                            checkTrade<Side::BUY>(seqNo, time, isSnapshot, localTimestamp, price, qty);
                        } else {
                            checkTrade<Side::SELL>(seqNo, time, isSnapshot, localTimestamp, price, qty);
                        }
                    } else {
                        ob.cancel(localTimestamp, price, p.flags.isBid ? Side::BUY : Side::SELL);
                    }

                    cursor = p.seqNo;
                    lastReceivedNs = p.localTimestamp;
                }
            }
            udpBuf.advance(ready);
            finalBufPos += sizeof(MDPacket);
        }

        assert(udpBuf.mask != ogMask);
        assert(udpBuf.lowestSeqNum >= ogLowestSeqNum);
        assert(udpBuf.lowestSeqNum == ogLowestSeqNum || udpBuf.head != ogHead);
        assert(multishotDone == ogMultishotDone);
        assert(ogUsed == receiveHdr.used);
        assert(finalBufPos - inBuf == numPackets * sizeof(MDPacket));

        return finalBufPos;

    }


    void completeMessage(io_uring_cqe &completion) {
        auto curTime = currentTimeNs();

        assert(io_uring_cq_ready(&ioState.ring) >= 1);
        assert(ioState.ring.cq.cqes == &completion);
        assert(mdFD > 2);
        assert(!udpBuf.test(0));
        assert(orderEntry.isConnected());

        u32 ogMask = udpBuf.mask;
        SeqNo ogSeqNo = udpBuf.lowestSeqNum;
        SeqNo ogCursor = cursor;

        i32 cRes = completion.res;
        u32 cFlags = completion.flags;
        u64 cUserData = io_uring_cqe_get_data64(&completion);

        assert(cRes > 0);
        assert(cFlags & IORING_CQE_F_BUFFER);
        assert(!(cFlags & IORING_CQE_F_NOTIF));
        assert(cUserData == ReceiveHeader::RECV_TAG);

        bool isAlive = isConnected();

        assert(cRes > 0 || -cRes != EBADF);
        assert(completion.flags & IORING_CQE_F_BUFFER);
        assert(!multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0));
        multishotDone = multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0);
        assert(isAlive == !(cRes > 0 || -cRes == EBADF));

        if (isAlive) {
            u8 *buf = receiveHdr.complete(completion);
            int bytesRead = completion.res;
            u64 numPackets = bytesRead / sizeof(MDPacket);
            assert(bytesRead % sizeof(MDPacket) == 0);
            u8 *endBuf = handleMessages(buf, numPackets, curTime);
            assert(endBuf == buf + sizeof(Order) * numPackets);
        }


        if (multishotDone && isAlive) {
            assert(receiveHdr.used.all());
            prepareRecv();
        }

        TimeNs now = currentTimeNs();
        assert(now - lastReceivedNs < 1'000'000);
        lastReceivedNs = now;

        assert(!udpBuf.test(0));
        assert(ogSeqNo == udpBuf.lowestSeqNum || cursor > ogCursor && ogMask != udpBuf.mask);
        assert(cursor >= ogCursor);
        assert(lastReceivedNs == now);
    }


    void prepareRecv() {
        receiveHdr.prepareRecv(ioState, mdFD);
        multishotDone = false;

    }

    [[nodiscard]] bool isConnected() const noexcept {
        TimeNs now = currentTimeNs();
        bool notDelayed = std::abs(now - lastReceivedNs) < 1000000;
        return !isComplete && mdFD != -1 && notDelayed;
    }
};


class Driver {

    StrategyState state = StrategyState::INIT;

    IOUringState ioState{};
    int fileTable[1];

    L2OB ob{};
    OE oe{ioState};
    Strat strat{ioState, oe, ob};


public:

    void run() {
        while (!strat.isComplete) {
            stateCheck();
            switch (state) {
                case StrategyState::INIT: {
                    assert(strat.mdFD != -1);
                    assert(!oe.isConnected());
                    assert(fileTable[0] != strat.mdFD);
                    fileTable[0] = strat.mdFD;
                    io_uring_register_files(&ioState.ring, fileTable, 1);

                    state = StrategyState::MD_CONNECT;
                    break;
                }
                case StrategyState::MD_CONNECT: {
                    assert(!oe.isConnected());
                    assert(strat.mdFD != -1);
                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(io_uring_cq_ready(&ioState.ring) == 0);
                    strat.prepareRecv();
                    io_uring_submit(&ioState.ring);
                    state = StrategyState::OE_CONNECT;
                    break;
                }
                case StrategyState::OE_CONNECT: {
                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    
                    if(int ready = io_uring_cq_ready(&ioState.ring) != 0) {
                        assert(ready == 1);
                        io_uring_cqe *entries;
                        if(io_uring_wait_cqe_nr(&ioState.ring, &entries, 1) == 0) {
                            io_uring_cqe &cqe = entries[0];
                            assert(cqe.res < 0);
                            assert(io_uring_cqe_get_data64(&cqe) == ReceiveHeader::RECV_TAG);
                            cout << "RECVMSG invalid (" << -cqe.res << ") " << strerror(-cqe.res) <<  endl;
                            exit(EXIT_FAILURE);
                        } else {
                            perror("Failed to request in error condition");
                            exit(EXIT_FAILURE);
                        }
                        
                    }

                    oe.establishConnection();
                    assert(oe.isConnected());
                    assert(strat.cursor == 0);
                    assert(strat.lastReceivedNs == 0);
                    assert(!strat.multishotDone);
                    state = StrategyState::RUNNING;
                    break;
                }
                case StrategyState::RUNNING: {
                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(!io_uring_cq_has_overflow(&ioState.ring));
                    assert(oe.isConnected());

                    if (u32 ready = io_uring_cq_ready(&ioState.ring)) {
                        io_uring_cqe *completions;
                        if (io_uring_wait_cqe_nr(&ioState.ring, &completions, ready) == 0) {
                            for (int i = 0; i < ready; ++i) {
                                io_uring_cqe &e = completions[i];
                                u64 userData = io_uring_cqe_get_data64(&e);
                                if (userData == ReceiveHeader::RECV_TAG) {
                                    strat.completeMessage(e);
                                } else {
                                    assert(userData >= OE::ORDER_TAG);
                                    oe.completeMessage(e);
                                }
                            }
                            io_uring_cq_advance(&ioState.ring, ready);
                        } else {
                            perror("Failed to get ready completions");
                            exit(EXIT_FAILURE);
                        }
                    }

                    assert(io_uring_cq_ready(&ioState.ring) == 0);
                    assert(strat.isConnected() || !strat.isComplete);
                    break;
                }
                default: {
                    cerr << "Unexpected state [" << int(state) << "]." << endl;
                    exit(EXIT_FAILURE);
                }

            }
        }
        cout << "Done" << endl;
    }

    bool stateCheck() {
        assert(strat.cursor >= 0);
        assert(strat.cursor == 0 || oe.isConnected() && strat.isConnected() && strat.lastReceivedNs > 0);
        assert(ioState.ring.sq.ring_entries == 256);
        assert(ioState.ring.ring_fd > 2);
        switch (state) {
            case StrategyState::INIT: {
                assert(!oe.isConnected());
                assert(!strat.isConnected());
                assert(strat.cursor == 0);
                assert(ob.seen.empty());
                assert(io_uring_sq_ready(&ioState.ring) == 0);
                assert(io_uring_cq_ready(&ioState.ring) == 0);
                break;
            }
            case StrategyState::MD_CONNECT: {
                assert(!oe.isConnected());
                assert(strat.cursor == 0);
                assert(!strat.isConnected());
                assert(ob.seen.empty());
                break;
            }
            case StrategyState::OE_CONNECT: {
                assert(strat.lastReceivedNs <= 0 || strat.isConnected());
                assert(!oe.isConnected());
                assert(strat.cursor == 0);
                assert(ob.seen.empty());
                break;
            }
            case StrategyState::RUNNING: {
                assert(oe.isConnected());
                assert(strat.lastReceivedNs <= 0 || strat.isConnected());
                assert(std::abs(currentTimeNs() - strat.lastReceivedNs) < 1'000'000);
                break;
            }
            default: {
                cerr << "Unexpected state [" << int(state) << "]." << endl;
                assert(false);
            }
        }
        return true;
    }
};

int main() {

    auto numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores < 0) {
        perror("sysconf");
        exit(EXIT_FAILURE);
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(PINNED_CPU, &cpuset);

    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
    int ret = sched_getaffinity(0, sizeof(cpuset), &cpuset);
    if (ret == -1) {
        perror("sched_getaffinity");
        exit(EXIT_FAILURE);
    }
    assert(CPU_ISSET(PINNED_CPU, &cpuset));

    Driver s;
    s.run();
}