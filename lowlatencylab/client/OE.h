//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_OE_H
#define LLL_EXCHANGE_OE_H

#include <cstring>
#include <arpa/inet.h>
#include "defs.h"
#include <netdb.h>

#include "TCPStack.h"
#include "IGB82576IO.h"

template<typename X>
class OE {
public:
    // TODO Use POLLHUP to determine when the other end has hung up

    constexpr static u64 ORDER_TAG = 3;

    constexpr static TCPConnConfig cfg{
	// .destMAC = {0x48, 0xd3, 0x43, 0xe9, 0x5c, 0xa0},
	// .srcMAC = {0x3c, 0xe9, 0xf7, 0xfe, 0xdf, 0x6c},
	// .sourceIP = {192, 168, 0, 104},
	// .destIP = {52, 56, 175, 121},
	.destMAC = {0xd2, 0x64, 0x11, 0xc3, 0x95, 0x51},
	.srcMAC = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56},
	.sourceIP = {192, 168, 100, 2},
	.destIP = {192, 168, 100, 1},
	.sourcePort = TCP_SENDING_PORT,
	.destPort = OE_PORT
   };


    TCP<X> tcp;
    OrderId orderId = 1;


    explicit OE(IGB82576IO<X, TCP<X>>& io): tcp{io, cfg} {
    }

    ~OE() {
        tcp.close();
    }

    void establishConnection() {
        if (ErrorCode err = tcp.establishTCPConnection(); err.isErr()) {
        	pr_info__("tcp error establish error detected, baliing");
        	swapcontext_(&tcp.io.ctxM.blockingRecvCtx, &tcp.io.ctxM.interruptCtx);
	        logErrAndExit(err);
        }
    }

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


        if (ErrorCode err = tcp.sendData(frame); err.isErr()) {
            logErrAndExit(err);
        }


        assert(isConnected());
    }

    [[nodiscard]] bool isConnected() const noexcept {
        return tcp.block.state == TCPState::ESTABLISHED;
    }
};

#endif //LLL_EXCHANGE_OE_H
