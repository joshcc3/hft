//
// Created by joshuacoutinho on 03/12/23.
//

#include "TCPStack.h"

int main() {
    // const string iface = "veth1";
    const string iface = "wlp0s20f3";
    const string pinPath = "/sys/fs/bpf/strat";

    XDPIO io{iface, pinPath};

    const TCPConnConfig cfg{
        .destMAC = {0x48, 0xd3, 0x43, 0xe9, 0x5c, 0xa0},
        .srcMAC = {0x3c, 0xe9, 0xf7, 0xfe, 0xdf, 0x6c},
        .sourceIP = {192, 168, 0, 104},
        .destIP = {52, 56, 175, 121},
        .sourcePort = TCP::SENDING_PORT,
        .destPort = OE_PORT
    };

    TCP tcp{io, cfg};

    if (ErrorCode err = tcp.establishTCPConnection(); err.isErr()) {
        logErrAndExit(err);
    }

    for (int i = 0; i < 1000; ++i) {
        std::stringstream s;
        s << i%10 << " <----\n";
        const string msg = s.str();
        constexpr int dataSz = 8;
        using PktType = PacketHdr<u8[dataSz]>;
        assert(msg.size() == dataSz);
        constexpr u32 packetSz = sizeof(PktType);
        PktType& outputBuf = *reinterpret_cast<PktType *>(tcp.getOutputBuf(packetSz));
        copy(msg.begin(), msg.end(), outputBuf.data);
        if (ErrorCode err = tcp.sendData(outputBuf, dataSz); err.isErr()) {
            logErrAndExit(err);
        }
        usleep(22'000);
    }
    tcp.close();
}
