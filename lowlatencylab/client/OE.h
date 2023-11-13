//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_OE_H
#define LLL_EXCHANGE_OE_H

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
#include "../defs.h"
#include "L2OB.h"
#include "mdmcclient.h"

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

        if (connect(clientFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
            perror("Bind to server socket failed.");
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
        assert(o.id == orderId - 1);
        assert(o.price == price);
        assert(o.qty == qty);

        io_uring_sqe *submitSqe = ioState.getSqe(o.id + ORDER_TAG);
        int sendFlags = MSG_DONTROUTE | MSG_DONTWAIT;
        io_uring_prep_send(submitSqe, clientFD, static_cast<void *>(outputBuf), msgSize, sendFlags);
        assert(submitSqe->flags == 0);

        assert(io_uring_sq_ready(&ioState.ring) == 1);

        int submits = io_uring_submit(&ioState.ring);
        assert(io_uring_sq_ready(&ioState.ring) == 0);
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
        assert(clientFD > 2);
        assert(orderId > 1);
        assert(curOrder.id >= 1);

        i32 cRes = completion.res;
        u32 cFlags = completion.flags;
        u64 cUserData = io_uring_cqe_get_data64(&completion);

        OrderId receivedId = cUserData - ORDER_TAG;

        if (receivedId == curOrder.id) {
            assert(cRes > 0);
            assert(!(cFlags & IORING_CQE_F_BUFFER));
            assert(!(cFlags & IORING_CQE_F_NOTIF));
            assert(cRes > 0 || -cRes != EBADF);

            // id, latency time,
//            cout << curOrder.id << "," << double(curTime - curOrder.triggerReceivedTime) / 1000.0 << ","
//                 << double(curTime - curOrder.submittedTime) / 1000.0 << '\n';
        } else {
//            cout << "Skipping [" << receivedId << "]." << '\n';
        }
    }
};

#endif //LLL_EXCHANGE_OE_H
