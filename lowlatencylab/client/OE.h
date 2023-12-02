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
#include <arpa/inet.h>
#include <netdb.h>

#include "XDPIO.h"

class OE {
public:
    // TODO Use POLLHUP to determine when the other end has hung up

    constexpr static u64 ORDER_TAG = 3;


    XDPIO& io;
    int clientFD = -1;
    OrderId orderId = 1;

    sockaddr_in serverAddr{};

    explicit OE(XDPIO& io, const std::string& oeHost): io{io} {
        const addrinfo hints{
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM
        };

        addrinfo* res{};

        int status;
        if ((status = getaddrinfo(oeHost.c_str(), nullptr, &hints, &res)) != 0) {
            std::cerr << "getaddrinfo: " << gai_strerror(status) << std::endl;
            return;
        }

        assert(res->ai_next == nullptr);
        assert(res != nullptr);
        assert(res->ai_family == AF_INET);

        serverAddr = *(sockaddr_in *) res->ai_addr;
        assert(serverAddr.sin_family == AF_INET);

        void* addr = &(serverAddr.sin_addr);
        char ipstr[16];
        inet_ntop(res->ai_family, addr, ipstr, sizeof ipstr);
        std::cout << "OE Server: " << ipstr << std::endl;

        serverAddr.sin_port = htons(OE_PORT);

        freeaddrinfo(res); // free the linked list
    }

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
        assert(clientFD == 7); // assume that this is the second socket opened

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

        assert(serverAddr.sin_addr.s_addr != 0);
        assert(serverAddr.sin_port != 0);

        if (connect(clientFD, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
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


        u8* outputBuf = io.getWriteBuff(sizeof(OrderFrame));

        TimeNs submitTime = currentTimeNs();
        OrderFrame& frame = *reinterpret_cast<OrderFrame *>(outputBuf);

        std::array<u8, ETH_ALEN> sourceMac = {0x3c, 0xe9, 0xf7, 0xfe, 0xdf, 0x6c};
        std::array<u8, ETH_ALEN> destMac = {0x48, 0xd3, 0x43, 0xe9, 0x5c, 0xa0};
        std::copy(sourceMac.begin(), sourceMac.end(), frame.eth.h_source);
        std::copy(destMac.begin(), destMac.end(), frame.eth.h_dest);

        frame.eth.h_proto = htons(ETH_P_IP);
        frame.ip.ihl = 5;
        frame.ip.version = 4;
        frame.ip.tos = 0;
        frame.ip.tot_len = htons(sizeof(OrderFrame) - sizeof(ethhdr));
        frame.ip.id = orderId;
        frame.ip.frag_off = 0x0;
        frame.ip.ttl = static_cast<u8>(255);
        frame.ip.protocol = 17;
        frame.ip.check = 0;
        constexpr u8 sourceIPBytes[4] = {192, 168, 0, 104};
        constexpr u8 destIPBytes[4] = {13, 40, 166, 252};
        const u32 sourceIP = *reinterpret_cast<const u32*>(sourceIPBytes);
        const u32 destIP = *reinterpret_cast<const u32*>(destIPBytes);
        frame.ip.saddr = sourceIP;
        frame.ip.daddr = destIP;
        const u8* dataptr = reinterpret_cast<u8 *>(&frame.ip);
        const u16 kernelcsum = ip_fast_csum(dataptr, frame.ip.ihl);
        frame.ip.check = kernelcsum;


        assert(frame.ip.check != 0);
        assert(umemLoc->eth.h_proto == htons(ETH_P_IP));
        assert(umemLoc->ip.frag_off == 0);
        assert(umemLoc->ip.ttl != 0);
        assert(umemLoc->ip.protocol == 17);
        assert(umemLoc->ip.tot_len == (htons(sizeof(PacketIn) - sizeof(ethhdr))));



        constexpr int udpPacketSz = sizeof(OrderFrame) - sizeof(ethhdr) - sizeof(iphdr);
        frame.udp.len = htons(udpPacketSz);
        frame.udp.check = 0;
        frame.udp.dest = htons(OE_PORT);
        frame.udp.source = htons(1234);


        frame.o.packetType = OE_PACKET_TYPE;
        frame.o.submittedTime = submitTime;
        frame.o.triggerEvent = triggerEvent;
        frame.o.triggerReceivedTime = triggerRecvTime;
        frame.o.id = orderId++;
        frame.o.price = price;
        frame.o.qty = qty;
        frame.o.flags = flags;

        frame.udp.check = 0;
        // frame.udp.check = udp_csum(umemLoc->ip.saddr, umemLoc->ip.daddr, umemLoc->udp.len,
                                      // IPPROTO_UDP, reinterpret_cast<u16 *>(&umemLoc->udp));

        assert(frame.o.submittedTime == submitTime);
        assert(frame.o.triggerEvent == triggerEvent);
        assert(frame.o.triggerReceivedTime == triggerRecvTime);
        assert(frame.o.id == orderId - 1);
        assert(frame.o.price == price);
        assert(frame.o.qty == qty);


        CLOCK(ORDER_SUBMISSION_PC,
            io.triggerWrite();
            io.complete();
        )

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

    void completeMessage(io_uring_cqe& completion) {
        auto curTime = currentTimeNs();

        assert(io_uring_cq_ready(&ioState.ring) >= 1);
        assert(clientFD > 2);
        assert(orderId > 1);
        assert(curOrder.id >= 1);

        i32 cRes = completion.res;
        u32 cFlags = completion.flags;
        u64 cUserData = io_uring_cqe_get_data64(&completion);

        OrderId receivedId = cUserData - ORDER_TAG;

    }
};

#endif //LLL_EXCHANGE_OE_H
