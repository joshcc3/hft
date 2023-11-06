//
// Created by jc on 04/11/23.
//

#include "exchmcserver.h"
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

enum class StrategyState {
    INIT,
    OE_CONNECT,
    MD_CONNECT,
    RUNNING
};

constexpr static int PINNED_CPU = 1;

class OE {
public:
    // TODO Use POLLHUP to determine when the other end has hung up

    IOUringState &ioState;
    int clientFD = -1;
    OrderId orderId = 1;

    sockaddr_in serverAddr;

    static constexpr size_t msgSize = sizeof(Order);
    char outputBuf[msgSize];
    Order curOrder;

    OE(IOUringState &ioState) : ioState{ioState} {}

    void establishConnection() {
        assert(clientFD == -1);
        assert(orderId == 1);
        assert(io_uring_sq_ready(&ioState.ring) == 0);
        assert(io_uring_cq_ready(&ioState.ring) == 0);

        clientFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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

        if (connect(clientFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

    }

    void submit(MDMsgId triggerEvent, TimeNs triggerRecvTime, PriceL price, Qty qty) {
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

        assert(o.submittedTime == submitTime);
        assert(o.triggerEvent == triggerEvent);
        assert(o.triggerReceivedTime == triggerRecvTime);
        assert(o.id == orderId);
        assert(o.price == price);
        assert(o.qty == qty);

        io_uring_sqe *submitSqe = ioState.getSqe(o.id);
        int flags = MSG_DONTROUTE | MSG_DONTWAIT;
        io_uring_prep_send(submitSqe, clientFD, static_cast<void *>(outputBuf), msgSize, flags);
        assert(submitSqe->flags == 0);

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
};


struct UDPBuffer {
    constexpr static int Sz = 32;
    static_assert(__builtin_popcount(Sz) == 1);
    static_assert(Sz == 32);

    using BufferItem = std::pair<int, std::string>;
    u32 mask = 0;
    int lowestSeqNum = 0;
    int head = 0;
    std::array<BufferItem, Sz> ringBuffer{};

    bool test(int pos) const {
        assert(pos < Sz && pos >= 0);
        return mask >> (1 << ((head + pos) & (Sz - 1)));
    }

    int newMessage(int seqNo, std::string &msg) {
        assert(head < Sz);
        assert(lowestSeqNum >= 0);
        assert((mask & u32(-1)) != u32(-1));
        assert(seqNo >= lowestSeqNum);
        int bufferOffs = seqNo - lowestSeqNum;
        assert(bufferOffs < Sz);
        int bufferPos = (head + bufferOffs) & (Sz - 1);
        int maskBit = 1 << bufferPos;
        assert((mask & maskBit) == 0);
        for (int i = 0; i < Sz; ++i) {
            if (test(i)) {
                assert(ringBuffer[i].first != -1);
            }
        }
        mask |= maskBit;
        u32 alignedMask = _rotl(mask, head);
        assert(_rotr(alignedMask, head) == mask);
        u32 fullMask = ((u64(1) << (bufferOffs + 1)) - 1) << (Sz - bufferOffs - 1);
        bool isFull = (fullMask & alignedMask) == fullMask;
        new(&ringBuffer[bufferPos]) BufferItem(seqNo, std::move(msg));
        return isFull ? bufferOffs + 1 : 0;
    }

    void complete(int n) {
        u32 alignedMask = _rotl(mask, head);
        assert(_rotr(alignedMask, head) == mask);
        u32 fullMask = ((u64(1) << n) - 1) << (Sz - n);
        bool isFull = (fullMask & alignedMask) == fullMask;
        assert(isFull);
        head = (head + n) & (Sz - 1);
    }
};

class Strat {
public:
    constexpr static int BUFFER_TAG = 0;
    constexpr static int RECV_TAG = 1;

    OE &orderEntry;
    IOUringState &ioState;
    TimeNs lastReceivedNs = -1;
    int mdFD = -1;
    int cursor = 0;
    UDPBuffer udpBuf{};

    static constexpr int BUFFER_SIZE = 1 << 12;
    static constexpr int NUM_BUFFERS = 16;
    static constexpr int GROUP_ID = 2;
    std::unique_ptr<u8[]> buffers;
    std::bitset<NUM_BUFFERS> used;
    bool multishotDone = true;

    Strat(IOUringState &ioState, OE &oe) : ioState{ioState},
                                        orderEntry{oe},
                                        buffers{std::make_unique<u8[]>(BUFFER_SIZE * NUM_BUFFERS)},
                                        used{} {

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
        addr.sin_addr.s_addr = htonl(INADDR_ANY); // Receive from any address
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

    u8 *handleMessages(u8 *inBuf, u64 numPackets, TimeNs time) {
        throw std::runtime_error("Not implemented");
        return nullptr;
    }


    void completeMessages(io_uring_cqe &completion) {
        auto curTime = currentTimeNs();

        assert(io_uring_cq_ready(&ioState.ring) >= 1);
        assert(ioState.ring.cq.cqes == &completion);
        assert(mdFD > 2);
        assert(!udpBuf.test(0));
        assert(orderEntry.isConnected());

        int ogMask = udpBuf.mask;
        int ogSeqNo = udpBuf.lowestSeqNum;
        int ogCursor = cursor;

        i32 cRes = completion.res;
        u32 cFlags = completion.flags;
        u64 cUserData = io_uring_cqe_get_data64(&completion);

        assert(cRes > 0);
        assert(cFlags & IORING_CQE_F_BUFFER);
        assert(!(cFlags & IORING_CQE_F_NOTIF));
        assert(cUserData == RECV_TAG);

        bool isAlive = isConnected();

        assert(cRes > 0 || cRes != EBADF);
        assert(completion.flags & IORING_CQE_F_BUFFER);
        assert(!multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0));
        multishotDone = multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0);
        assert(isAlive == !(cRes > 0 || cRes == EBADF));

        if (isAlive) {
            u32 bufferIx = completion.flags >> (sizeof(completion.flags) * 8 - 16);
            assert(bufferIx < NUM_BUFFERS);
            assert(!used.test(bufferIx));

            u8 *buf = buffers.get() + bufferIx * BUFFER_SIZE;
            int bytesRead = cRes;
            u64 numPackets = cRes / sizeof(MDPacket);
            assert(cRes % sizeof(MDPacket) == 0);


            u8 *endBuf = handleMessages(buf, numPackets, curTime);
            assert(endBuf == buf + sizeof(Order) * numPackets);

            used.set(bufferIx);
        }


        if (multishotDone && isAlive) {
            assert(used.all());
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

        assert(used.all() || !used.any());
        used.reset();

        io_uring_sqe *bufferSqe = ioState.getSqe(BUFFER_TAG);
        io_uring_prep_provide_buffers(bufferSqe, buffers.get(), BUFFER_SIZE, NUM_BUFFERS, GROUP_ID, 0);
        assert(io_uring_sq_ready(&ioState.ring) == 1);
        int completed = io_uring_submit_and_wait(&ioState.ring, 1);
        assert(completed == 1);
        assert(io_uring_sq_ready(&ioState.ring) == 0);
        {
            cqe_guard g{ioState};
            assert(g.completion->res == NUM_BUFFERS || g.completion->res == 0);
            assert((io_uring_cqe_get_data64(g.completion)) == BUFFER_TAG);

        }

        assert(mdFD != -1);
        io_uring_sqe *recvSqe = ioState.getSqe(RECV_TAG);
        io_uring_prep_recv_multishot(recvSqe, mdFD, nullptr, 0, 0);
        assert(recvSqe->flags == 0);
        int ogFlags = recvSqe->flags;
        recvSqe->flags |= IOSQE_BUFFER_SELECT | IORING_RECVSEND_POLL_FIRST;
        assert(recvSqe->flags == (ogFlags | IOSQE_BUFFER_SELECT));
        recvSqe->buf_group = GROUP_ID;
        multishotDone = false;

    }

    [[nodiscard]] bool isConnected() const noexcept {
        TimeNs now = currentTimeNs();
        bool notDelayed = std::abs(now - lastReceivedNs) < 1000000;
        return mdFD != -1 && notDelayed;
    }
};

class Driver {

    StrategyState state = StrategyState::INIT;

    IOUringState ioState{};
    L2OB ob{};

    OE oe;
    Strat strat;


public:

    void run() {
        while (true) {
            switch (state) {
                case StrategyState::INIT: {
                    break;
                }
                case StrategyState::OE_CONNECT: {
                    break;
                }
                case StrategyState::MD_CONNECT: {
                    break;
                }
                case StrategyState::RUNNING: {
                    break;
                }
                default: {
                    cerr << "Unexpected state [" << int(state) << "]." << endl;
                    exit(EXIT_FAILURE);
                }

            }
        }
    }

    bool stateCheck() {
        assert(strat.cursor >= 0);
        assert(strat.cursor == 0 || oe.isConnected() && strat.isConnected() && strat.lastReceivedNs > 0);
        switch (state) {
            case StrategyState::INIT: {
                assert(!oe.isConnected());
                assert(!strat.isConnected());
                assert(strat.cursor == 0);
                assert(ob.seen.empty());
                assert(ioState.ring.ring_fd > 2);
                assert(ioState.ring.sq.ring_entries == 256);
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
                assert(strat.isConnected());
                assert(!oe.isConnected());
                assert(strat.cursor == 0);
                assert(ob.seen.empty());
                break;
            }
            case StrategyState::RUNNING: {
                assert(oe.isConnected());
                assert(strat.isConnected());
                assert(std::abs(currentTimeNs() - strat.lastReceivedNs) < 1'000'000);
                break;
            }
            default: {
                cerr << "Unexpected state [" << int(state) << "]." << endl;
                assert(false);
            }
        }
    }
};

int main() {
    // TODO - set cpu affinity
    /*
     * #define _GNU_SOURCE // Required for sched_setaffinity
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    // Get the number of available CPU cores (logical processors)
    int numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores < 0) {
        perror("sysconf");
        exit(EXIT_FAILURE);
    }

    // Create a CPU set to specify the CPU affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset); // Initialize the set to zero

    // Set the CPU affinity to the first CPU core (CPU 0)
    CPU_SET(0, &cpuset);

    // Apply the CPU affinity to the current process (or your specific PID)
    pid_t pid = getpid(); // Get the process ID of the current process
    if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }

    // Check if the affinity was set successfully
    int ret = sched_getaffinity(pid, sizeof(cpuset), &cpuset);
    if (ret == -1) {
        perror("sched_getaffinity");
        exit(EXIT_FAILURE);
    }

    // Print the CPU affinity mask for the process
    for (int i = 0; i < numCores; i++) {
        if (CPU_ISSET(i, &cpuset)) {
            printf("CPU %d: Yes\n", i);
        } else {
            printf("CPU %d: No\n", i);
        }
    }

    // Continue with your application...

    return 0;
}

     */
    Strategy s;
    s.run();
}