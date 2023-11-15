//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_OESERVER_H
#define LLL_EXCHANGE_OESERVER_H

#include <bitset>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <liburing.h>
#include <cstdlib>
#include <cassert>
#include <netinet/in.h>
#include <vector>
#include <utility>
#include <mutex>
#include <thread>
#include <string>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <memory>
#include "../defs.h"
#include "MDServer.h"


struct LabResult {
    TimeNs connectionTime;
    TimeNs disconnectTime;
    std::vector<OrderInfo> seenOrders;

    LabResult() : connectionTime{0}, disconnectTime{0}, seenOrders{} {}

    [[nodiscard]] bool empty() const {
        assert(seenOrders.empty() == (connectionTime == 0));
        return seenOrders.empty();
    }
};


class OEServer {
public:
    constexpr static int ACCEPT_TAG = 0;
    constexpr static int BUFFER_TAG = 1;
    constexpr static int RECV_TAG = 2;

    IOUringState &ioState;
    const MDServer &md;

    bool isAlive;
    int serverFD = -1;
    sockaddr_in serverAddr{};

    static constexpr int BUFFER_SIZE = 1 << 9;
    static constexpr int NUM_BUFFERS = 10;
    static constexpr int GROUP_ID = 1;
    std::unique_ptr<char[]> buffers;
    std::bitset<NUM_BUFFERS> used;
    int connectionSeen = 0;
    int clientFD = -1;
    sockaddr_in clientAddr{};
    socklen_t clientAddrSz = sizeof(clientAddr);

    LabResult curLabResult{};

    std::ofstream file_out;


    OEServer(IOUringState &ioState, const MDServer &md, const std::string &outFileName) : ioState{ioState}, md{md},
                                                                                          isAlive{false},
                                                                                          buffers{std::make_unique<char[]>(
                                                                                                  BUFFER_SIZE *
                                                                                                  NUM_BUFFERS)},
                                                                                          used{},
                                                                                          file_out{outFileName} {

        serverFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (serverFD == -1) {
            cerr << "Could not create oe server [" << errno << "]" << endl;
            exit(EXIT_FAILURE);
        }
        assert(serverFD > 2); // assume that this is the second socket opened

        int enable = 1;
        if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
            throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
        }
        enable = 1;
        if (setsockopt(serverFD, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) < 0) {
            throw std::runtime_error("setsockopt(SO_KEEPALIVE) failed");
        }
        int initialProbe = 10;
        if (setsockopt(serverFD, IPPROTO_TCP, TCP_KEEPIDLE, &initialProbe, sizeof(initialProbe)) < 0) {
            throw std::runtime_error("setsockopt(TCP_KEEPIDLE) failed");
        }
        int subsequentProbes = 2;
        if (setsockopt(serverFD, IPPROTO_TCP, TCP_KEEPINTVL, &subsequentProbes, sizeof(subsequentProbes)) < 0) {
            throw std::runtime_error("setsockopt(TCP_KEEPINTVL) failed");
        }
        int maxProbes = 2;
        if (setsockopt(serverFD, IPPROTO_TCP, TCP_KEEPCNT, &maxProbes, sizeof(maxProbes)) < 0) {
            throw std::runtime_error("setsockopt(TCP_KEEPCNT) failed");
        }


        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(OE_PORT);

        if (bind(serverFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        if (listen(serverFD, 1) < 0) {
            throw std::runtime_error("listen(serverDF, 1) failed");
        }

        if (!file_out.is_open()) {
            std::cerr << "Could not open the file " << outFileName << " for writing." << std::endl;
            exit(EXIT_FAILURE);
        }

        file_out
                << "connectionSeen,curLabResult.connectionTime,curLabResult.disconnectTime,orderInfo.orderInfo.submittedTime,orderInfo.receivedTime,orderInfo.orderInfo.triggerReceivedTime,orderInfo.triggerSubmitTime,orderInfo.orderInfo.triggerEvent,orderInfo.orderInfo.flags.isBid,orderInfo.orderInfo.price,orderInfo.orderInfo.qty,orderInfo.orderInfo.id\n";

        reset();

    }

    ~OEServer() {
        reset();
        if (serverFD != -1) {
            close(serverFD);
            serverFD = -1;
        }
    }

    void prepAccept() {
        assert(clientFD == -1);
        assert(!isAlive);
        assert(clientAddrSz > 0);
        assert(curLabResult.seenOrders.empty());
        assert(curLabResult.connectionTime == 0);
        assert(curLabResult.disconnectTime == 0);

        io_uring_sqe *acceptSqe = ioState.getSqe(ACCEPT_TAG);
        clientAddrSz = sizeof(clientAddr);
        assert(fcntl(serverFD, F_GETFD) != -1);
        io_uring_prep_accept(acceptSqe, serverFD, (struct sockaddr *) &clientAddr, &clientAddrSz,
                             SOCK_NONBLOCK | O_CLOEXEC);
    }

    void receiveConn(io_uring_cqe *completion) {
        isAlive = true;
        ++connectionSeen;
        clientFD = completion->res;

        curLabResult.connectionTime = currentTimeNs();
        if (clientFD == -1) {
            cerr << "Failed to accept [" << -clientFD << "]." << endl;
            exit(EXIT_FAILURE);
        }
        assert((io_uring_cqe_get_data64(completion)) == ACCEPT_TAG);
    }

    [[nodiscard]] bool connectionAlive() const {
        return isAlive;
    }

    void reset() {
        assert(!isAlive);
        int lastUsedBuffer = -1;
        while (++lastUsedBuffer < NUM_BUFFERS && used.test(lastUsedBuffer));
        for (int i = lastUsedBuffer + 1; i < NUM_BUFFERS; ++i) {
            assert(!used.test(i));
        }
        assert(lastUsedBuffer >= 0 && lastUsedBuffer <= NUM_BUFFERS);
        assert(lastUsedBuffer == NUM_BUFFERS || !used.test(lastUsedBuffer));
        if (lastUsedBuffer != 0) {
            assert(io_uring_sq_ready(&ioState.ring) == 0);
            assert(io_uring_cq_ready(&ioState.ring) == 0);
            io_uring_sqe *bufferSqe = ioState.getSqe(BUFFER_TAG);
            int freeBuffers = int(used.size()) - lastUsedBuffer;
            assert(freeBuffers < NUM_BUFFERS);
            io_uring_prep_remove_buffers(bufferSqe, freeBuffers, GROUP_ID);
            assert(io_uring_sq_ready(&ioState.ring) == 1);
            int completed = ioState.submitAndWait(1);
            assert(completed == 1);
            {
                cqe_guard g{ioState};
                auto completionRes = g.completion->res;
                u64 userData = g.completion->user_data;
                if (userData != BUFFER_TAG || completionRes != freeBuffers) {
                    cerr << "Failed to dealloc buffers [" << completionRes << "] [" << userData << "]." << endl;
                    assert(false);
                }
            }
        }
        used.reset();

        curLabResult.disconnectTime = currentTimeNs();


        for (const auto &orderInfo: curLabResult.seenOrders) {

            file_out << connectionSeen << "," << curLabResult.connectionTime << "," << curLabResult.disconnectTime
                     << "," <<
                     orderInfo.orderInfo.submittedTime << "," << orderInfo.receivedTime << "," <<
                     orderInfo.orderInfo.triggerReceivedTime << "," << orderInfo.triggerSubmitTime << "," <<
                     orderInfo.orderInfo.triggerEvent << "," << orderInfo.orderInfo.flags.isBid << ","
                     << orderInfo.orderInfo.price << "," <<
                     orderInfo.orderInfo.qty << "," << orderInfo.orderInfo.id << "\n";
        }
        file_out.flush();
        if (clientFD != -1) {
            close(clientFD);
            clientFD = -1;
        }
        isAlive = false;
        clientFD = -1;
        clientAddrSz = sizeof(clientAddr);
        curLabResult.seenOrders.clear();
        curLabResult.connectionTime = 0;
        curLabResult.disconnectTime = 0;

    }

    void prepareRecv() {
        assert(used.all() || !used.any());
        used.reset();
        for (int i = 0; i < used.size(); ++i) {
            assert(!used.test(i));
        }

        io_uring_sqe *bufferSqe = ioState.getSqe(BUFFER_TAG);
        io_uring_prep_provide_buffers(bufferSqe, buffers.get(), BUFFER_SIZE, NUM_BUFFERS, GROUP_ID, 0);
        assert(io_uring_sq_ready(&ioState.ring) == 1);

        assert(clientFD != -1);
        assert(isAlive);
        io_uring_sqe *recvSqe = ioState.getSqe(RECV_TAG);
        io_uring_prep_recv_multishot(recvSqe, clientFD, nullptr, 0, 0);
        recvSqe->buf_group = GROUP_ID;
        assert(recvSqe->flags == 0);
        int ogFlags = recvSqe->flags;
        recvSqe->flags |= IOSQE_BUFFER_SELECT;
        assert(recvSqe->flags == (ogFlags | IOSQE_BUFFER_SELECT));
        recvSqe->buf_group = GROUP_ID;
    }

    void completeMessages(io_uring_cqe &completion) {
        assert(isAlive);
        assert(connectionSeen > 0);
        assert(curLabResult.connectionTime > 0);


        auto curTime = currentTimeNs();

        std::size_t ogLabResSize = curLabResult.seenOrders.size();

        u64 userData = io_uring_cqe_get_data64(&completion);

        bool multishotDone = false;
        assert(userData == RECV_TAG);
        int returnCode = completion.res;
        assert(returnCode >= 0 || -returnCode == EBADF || -returnCode == ENOBUFS);
        assert(!multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0));
        multishotDone = ((completion.flags & IORING_CQE_F_MORE) == 0);
        assert(isAlive || returnCode >= 0 || -returnCode == EBADF || -returnCode == ENOBUFS);
        isAlive = isAlive && (returnCode > 0 || returnCode == -ENOBUFS);
        assert(!isAlive || -returnCode == ENOBUFS || completion.flags & IORING_CQE_F_BUFFER);

        if (isAlive && returnCode > 0) {
            u32 bufferIx = completion.flags >> (sizeof(completion.flags) * 8 - 16);
            assert(bufferIx < NUM_BUFFERS);
            assert(!used.test(bufferIx));

            char *buf = buffers.get() + bufferIx * BUFFER_SIZE;
            int bytesRead = returnCode;
            u32 numMessages = bytesRead / sizeof(Order);
            assert(bytesRead % sizeof(Order) == 0);
            assert(bytesRead <= BUFFER_SIZE);

            char *endBuf = handleMessages(buf, numMessages, curLabResult, curTime);
            assert(endBuf == buf + sizeof(Order) * numMessages);

            used.set(bufferIx);
            assert(!isAlive || curLabResult.seenOrders.size() > ogLabResSize);
            assert(io_uring_sq_ready(&ioState.ring) == 0);
        } else if (!isAlive) {
            assert(returnCode == 0 || -returnCode == EBADF);
            reset();
        } else if (-returnCode == ENOBUFS) {
            assert(multishotDone && isAlive);
        } else {
            assert(false);
        }

        if (multishotDone && isAlive) {
            assert(used.all());
            prepareRecv();
            ioState.submit();
        }


    }

private:
    char *handleMessages(char *buf, u32 n, LabResult &lab, TimeNs curTime) {
        for (int i = 0; i < n; ++i) {

            const auto *o = reinterpret_cast<Order *>(buf);
            auto timeNow = currentTimeNs();
            OrderId orderId = o->id;
            TimeNs triggerSubmitTime = md.eventTime(o->triggerEvent);

            assert(std::abs(o->submittedTime - timeNow) < 1e9);
            assert(md.validEvent(o->triggerEvent));
            assert(std::abs(o->triggerReceivedTime - triggerSubmitTime));
            // TODO: For some reason if an enobufs is delivered even after a packet has been received, its redelivered
//            assert(o->id > 0 && std::find_if(curLabResult.seenOrders.begin(), curLabResult.seenOrders.end(),
//                                             [orderId](const OrderInfo &order) {
//                                                 return order.orderInfo.id == orderId;
//                                             }) ==
//                                curLabResult.seenOrders.end());
            assert(o->price > 0);
            assert(o->qty > 0);

            lab.seenOrders.emplace_back(*o, curTime, triggerSubmitTime);
            buf += sizeof(Order);
        }

        return buf;
    }

};

#endif //LLL_EXCHANGE_OESERVER_H
