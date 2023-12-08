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

#include "TCPStack.h"
#include "XDPIO.h"

class OE {
public:
    // TODO Use POLLHUP to determine when the other end has hung up

    constexpr static u64 ORDER_TAG = 3;

    const TCPConnConfig cfg{
        .destMAC = {0x0, 0x0, 0x0, 0x0, 0x0, 0x2},
        .srcMAC = {0x0, 0x0, 0x0, 0x0, 0x0, 0x1},
        .sourceIP = {10, 0, 0, 1},
        .destIP = {10, 0, 0, 2},
        .sourcePort = TCP::SENDING_PORT,
        .destPort = OE_PORT
    };

    TCP tcp;
    OrderId orderId = 1;


    explicit OE(XDPIO& io, const std::string& oeHost): tcp{io, cfg} {
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

        sockaddr_in serverAddr = *(sockaddr_in *) res->ai_addr;
        assert(serverAddr.sin_family == AF_INET);

        void* addr = &(serverAddr.sin_addr);
        char ipstr[16];
        inet_ntop(res->ai_family, addr, ipstr, sizeof ipstr);
        std::cout << "OE Server: " << ipstr << std::endl;

        assert(serverAddr.sin_addr.s_addr == *reinterpret_cast<const u32*>(&cfg.destIP));
    }

    ~OE() {
        tcp.close();
    }

    void establishConnection() {
        if (ErrorCode err = tcp.establishTCPConnection(); err.isErr()) {
            logErrAndExit(err);
        }
    }

    template<bool IsReal>
    void submit(MDMsgId triggerEvent, TimeNs triggerRecvTime, PriceL price, Qty qty, OrderFlags flags) {
        /*
            TODO - setup queue polling to avoid the system call for the kernel to automatically
            pick up submits.
        */


        u8* outputBuf = tcp.getOutputBuf(sizeof(OrderFrame));

        TimeNs submitTime = currentTimeNs();
        OrderFrame& frame = *reinterpret_cast<OrderFrame *>(outputBuf);

        frame.data.packetType = OE_PACKET_TYPE;
        frame.data.submittedTime = submitTime;
        frame.data.triggerEvent = triggerEvent;
        frame.data.triggerReceivedTime = triggerRecvTime;
        frame.data.id = orderId++;
        frame.data.price = price;
        frame.data.qty = qty;
        frame.data.flags = flags;

        assert(frame.data.submittedTime == submitTime);
        assert(frame.data.triggerEvent == triggerEvent);
        assert(frame.data.triggerReceivedTime == triggerRecvTime);
        assert(frame.data.id == orderId - 1);
        assert(frame.data.price == price);
        assert(frame.data.qty == qty);


        if (ErrorCode err = tcp.sendData<IsReal>(frame); err.isErr()) {
            logErrAndExit(err);
        }
        if constexpr (!IsReal) {
            tcp.io.cancelPrevWriteBuff();
        }


        assert(isConnected());
    }

    [[nodiscard]] bool isConnected() const noexcept {
        return tcp.block.state == TCPState::ESTABLISHED;
    }
};

#endif //LLL_EXCHANGE_OE_H
