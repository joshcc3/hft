//
// Created by joshuacoutinho on 03/12/23.
//

#ifndef TCP_H
#define TCP_H
#include <netinet/tcp.h>

#include "XDPIO.h"

using namespace std;

struct TCPConnConfig {
    const u8 destMAC[6]{};
    const u8 srcMAC[6]{};
    const u8 sourceIP[4]{};
    const u8 destIP[4]{};
    const u16 sourcePort{};
    const u16 destPort{};
};


struct ErrorCode {
    u8 RECV_SYN: 1;
    u8 RECV_OLD_SEQ: 1;
    u8 RECV_ACK: 1;
    u8 RECV_WINDOW: 1;
    u8 RECV_IS_RST: 1;
    u8 RECV_UNEXPECTED_ACK: 1;

    void clear() {
        *reinterpret_cast<u8 *>(this) = 0;
    }

    bool isErr() const {
        return *reinterpret_cast<const u8 *>(this);
    }
};

inline void logErrAndExit(ErrorCode& err) {
    printf("Error while receiving data [0x%x]\n", *reinterpret_cast<u8 *>(&err));
    exit(EXIT_FAILURE);
}


enum class TCPState {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN1,
    FIN2
};

template<typename T>
struct PacketHdr {
    ethhdr eth;
    iphdr ip;
    tcphdr tcp;
    T data;

    [[nodiscard]] u32 dataSz() const {
        return ntohs(ip.tot_len) - ip.ihl * 4 - tcp.doff * 4;
    }
} __attribute((packed));

using TCPSeqNo = u32;
using SegmentSz = u32;

struct TCB {
    constexpr static u32 RCV_WINDOW_SZ = 1 << 15;
    constexpr static u32 SND_WINDOW_SZ = 1 << 12;

    /*
    *SND.UNA	send unacknowledged
    SND.NXT	send next
    SND.WND	send window
    SND.UP	send urgent pointer
    SND.WL1	segment sequence number used for last window update
    SND.WL2	segment acknowledgment number used for last window update
    ISS	initial send sequence number
    Table 3: Receive Sequence Variables
    Variable	Description
    RCV.NXT	receive next
    RCV.WND	receive window
    RCV.UP	receive urgent pointer
    IRS	initial receive sequence number
    */
    u32 sndUNA{};
    u32 sndNXT{};
    u32 sndWND{SND_WINDOW_SZ};
    u32 sndWL1{};
    u32 sndWL2{};
    u32 isn{};

    u32 rcvNXT{};
    u32 rcvWND{RCV_WINDOW_SZ};
    u32 irs{};
};

inline bool operator==(const TCB& b1, const TCB& b2) {
    return b1.sndUNA == b2.sndUNA &&
           b1.sndNXT == b2.sndNXT &&
           b1.sndWND == b2.sndWND &&
           b1.sndWL1 == b2.sndWL1 &&
           b1.sndWL2 == b2.sndWL2 &&
           b1.isn == b2.isn &&
           b1.rcvNXT == b2.rcvNXT &&
           b1.rcvWND == b2.rcvWND &&
           b1.irs == b2.irs;
}


// TODO rto
class TCP {
    static_assert(sizeof(ErrorCode) == 1);

public:
    constexpr static u16 SENDING_PORT = 36262;

    const TCPConnConfig cfg;

    XDPIO& io;

    TCPState state{TCPState::CLOSED};
    TCB block;
    u32 ipMsgId{0};

    SegmentSz peerMSS{536};
    SegmentSz ourMSS{536};

    TCP(XDPIO& io, const TCPConnConfig cfg): cfg{cfg}, io{io} {
        block.isn = isnGen();
        block.sndNXT = block.isn;
    }

    static TCPSeqNo isnGen() {
        const u64 timeNowNs = chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return static_cast<TCPSeqNo>(timeNowNs / 4'000);
    }


    template<typename T>
    void parseTCPOptions(const PacketHdr<T>* packet, __u32 len) {
        const u8* optionsOffsPtr = reinterpret_cast<const u8 *>(&packet->data);
        const int optionsSz = packet->tcp.doff * 4 - sizeof(tcphdr);
        int optionsOffs = 0;
        while (optionsOffs < optionsSz) {
            const u8* optionByte = optionsOffsPtr + optionsOffs;
            if (*optionByte == 0) {
                break;
            }
            if (*optionByte == 1) {
                ++optionsOffs;
            } else if (*optionByte == 2) {
                assert(*(optionByte + 1) == 4);
                peerMSS = ntohs(*reinterpret_cast<const u16 *>(optionByte + 2));
                cout << "Peer MSS [" << peerMSS << "]." << endl;
                optionsOffs += 4;
            } else {
                const int tcpOptionLen = *(optionByte + 1);
                cout << "TCP Option [" << *optionByte << " (" << tcpOptionLen << ")]." << endl;
                optionsOffs += tcpOptionLen;
            }
            assert(optionsOffs <= optionsSz);
        }
        for (const u8* i = optionsOffsPtr + optionsOffs; i < optionsOffsPtr + optionsSz; ++i) {
            assert(*i == 0);
        }
    }


    template<typename T>
    void prepareMsg(PacketHdr<T>& frame, int packetSz, bool isSyn, bool isFin) {
        std::copy(cfg.srcMAC, cfg.srcMAC + ETH_ALEN, frame.eth.h_source);
        std::copy(cfg.destMAC, cfg.destMAC + ETH_ALEN, frame.eth.h_dest);

        frame.eth.h_proto = htons(ETH_P_IP);
        frame.ip.ihl = 5;
        frame.ip.version = 4;
        frame.ip.tos = 0;
        frame.ip.tot_len = htons(packetSz - sizeof(ethhdr));
        frame.ip.id = ipMsgId;
        frame.ip.frag_off = 0x0;
        frame.ip.ttl = IPDEFTTL;
        frame.ip.protocol = IPPROTO_TCP;
        frame.ip.check = 0;
        frame.ip.saddr = *reinterpret_cast<const u32 *>(cfg.sourceIP);
        frame.ip.daddr = *reinterpret_cast<const u32 *>(cfg.destIP);
        const u8* dataptr = reinterpret_cast<u8 *>(&frame.ip);
        const u16 kernelcsum = ip_fast_csum(dataptr, frame.ip.ihl);
        frame.ip.check = kernelcsum;

        frame.tcp.source = htons(cfg.sourcePort);
        frame.tcp.dest = htons(cfg.destPort);
        frame.tcp.seq = htonl(block.sndNXT);
        frame.tcp.ack_seq = htonl(block.rcvNXT);
        assert(sizeof(tcphdr) % 4 == 0);
        frame.tcp.doff = sizeof(tcphdr) / 4;
        frame.tcp.res1 = 0;
        frame.tcp.res2 = 0;
        frame.tcp.urg = 0;
        frame.tcp.ack = 1;
        frame.tcp.psh = 0;
        frame.tcp.rst = 0;
        frame.tcp.syn = isSyn;
        frame.tcp.fin = isFin;
        frame.tcp.window = htons(TCB::RCV_WINDOW_SZ);
        frame.tcp.check = 0;
        frame.tcp.urg_ptr = 0;

        block.sndNXT += max(static_cast<u32>(sizeof(T)), static_cast<u32>(isSyn) | static_cast<u32>(isFin));
        ++ipMsgId;

        assert(frame.ip.check != 0);
        assert(frame.eth.h_proto == htons(ETH_P_IP));
        assert(frame.ip.frag_off == 0);
        assert(frame.ip.ttl != 0);
        assert(frame.ip.protocol == IPPROTO_TCP);
        assert(frame.ip.tot_len == htons(packetSz - sizeof(ethhdr)));
    }


    void prepareSyn() {
        using PktType = PacketHdr<u8[0]>;
        constexpr int packetSz = sizeof(PktType);

        u8* outputBuf = io.getWriteBuff(packetSz);

        PktType& frame = *reinterpret_cast<PktType *>(outputBuf);
        prepareMsg(frame, packetSz, true, false);

        frame.tcp.ack_seq = 0;
        frame.tcp.ack = 0;
        frame.tcp.syn = 1;

        setTCPCsum(frame);

        block.sndUNA = block.sndNXT;
        assert(block.sndWND == TCB::SND_WINDOW_SZ);
        assert(block.sndNXT > block.isn);

        state = TCPState::SYN_SENT;
    }

    template<typename T>
    [[nodiscard]] ErrorCode rcvPktCheck(const PacketHdr<T>* packet, u32 bytesReadWithPhy) {
        const u32 ackSeq = ntohl(packet->tcp.ack_seq);
        const u32 senderSeq = ntohl(packet->tcp.seq);

        const ErrorCode err{
            .RECV_OLD_SEQ = senderSeq < block.rcvNXT,
            .RECV_ACK = static_cast<u32>(ackSeq) < block.sndUNA,
            .RECV_IS_RST = packet->tcp.rst != 0 || state != TCPState::FIN1 && packet->tcp.fin,
            .RECV_UNEXPECTED_ACK = packet->tcp.ack != 1
        };
        if (__builtin_expect(err.isErr(), false)) {
            return err;
        }

        assert(packet->eth.h_proto == htons(ETH_P_IP));
        assert(packet->ip.version == 4);
        assert(packet->ip.ihl == 5);
        assert(
            ntohs(packet->ip.tot_len) <= bytesReadWithPhy - sizeof(ethhdr) && ntohs(packet->ip.tot_len) + 8 >=
            bytesReadWithPhy - sizeof(ethhdr));
        assert((ntohs(packet->ip.frag_off) & 0x1fff) == 0);
        assert(((packet->ip.frag_off >> 13) & 1) == 0);
        assert(packet->tcp.source == htons(OE_PORT));
        assert(packet->tcp.dest == htons(SENDING_PORT));
        assert(packet->tcp.doff <= 10);
        assert(packet->tcp.res1 == 0);
        assert(packet->tcp.res2 == 0);
        assert(packet->tcp.urg == 0);
        assert(ntohs(packet->tcp.window) >= 128 && ntohs(packet->tcp.window) <= 65536);
        assert(packet->tcp.urg_ptr == 0);


        return err;
    }


    template<typename PktType, bool Blocking, typename RcvHandler>
    [[nodiscard]] ErrorCode waitAck(const RcvHandler& handler) {
        io.qs.stateCheck();
        io.umem.stateCheck();
        io.socket.stateCheck();

        // if we get multiple out of order packets then we should process them smartly - last to first.
        const auto& [available, reserved, idx, fillQIdx] = io.recv<Blocking>();

        if (available > 0) {
            assert(reserved == available);
            const auto [readDesc, readAddr] = io.getReadDesc(idx);

            const __u32 len = readDesc->len;
            const u8* inBuf = readAddr;
            u32 bytesReadWithPhy = len;
            assert(inBuf != nullptr);

            assert(bytesReadWithPhy > 0);
            assert(bytesReadWithPhy <= ourMSS - sizeof(ethhdr) - sizeof(iphdr) - sizeof(tcphdr));

            const auto* packet = reinterpret_cast<const PktType *>(inBuf);
            if (const ErrorCode err = rcvPktCheck(packet, bytesReadWithPhy); err.isErr()) {
                return err;
            }
            parseTCPOptions(packet, len);
            block.rcvNXT = ntohl(packet->tcp.seq) + max(packet->dataSz(),
                                                        static_cast<u32>(packet->tcp.syn) | static_cast<u32>(packet->tcp
                                                            .
                                                            fin));
            block.rcvWND = ntohs(packet->tcp.window);
            block.sndUNA = ntohl(packet->tcp.ack_seq);
            const ErrorCode err = handler(packet);

            io.releaseSingleFrame(readDesc->addr, fillQIdx);

            return err;
        } else {
            return {};
        }
    }

    [[nodiscard]] ErrorCode synRecv(const PacketHdr<u8[0]>* packet) {
        assert(block.sndUNA == block.sndNXT);
        assert(state == TCPState::SYN_SENT);
        assert(ipMsgId == 1);

        block.irs = ntohl(packet->tcp.seq);
        block.rcvNXT = ntohl(packet->tcp.seq) + 1;
        block.sndUNA = block.sndNXT;

        assert(block.sndUNA == block.sndNXT);
        assert(block.irs == ntohl(packet->tcp.seq));
        assert(block.rcvWND == ntohs(packet->tcp.window));
        assert(peerMSS >= 500 && peerMSS <= 1500);
        assert(block.rcvWND >= 1024 && block.rcvWND <= 1 << 16);

        return {
            .RECV_SYN = packet->tcp.syn != 1,
            .RECV_OLD_SEQ = block.rcvNXT != ntohl(packet->tcp.seq) + 1,
        };
    }

    void prepareCompleteHandshake() {
        using PktType = PacketHdr<u8[0]>;
        constexpr int packetSz = sizeof(PktType);

        u8* outputBuf = io.getWriteBuff(packetSz);

        const u32 ogSeqNo = block.sndNXT;
        PktType& frame = *reinterpret_cast<PktType *>(outputBuf);
        prepareMsg(frame, packetSz, false, false);

        setTCPCsum(frame);

        assert(block.sndUNA == ogSeqNo);
        assert(block.sndNXT == ogSeqNo);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);
        assert(block.sndNXT > block.isn);
        assert(frame.dataSz() == 0);

        state = TCPState::ESTABLISHED;
    }

    template<typename PktType>
    void prepareDataSend(PktType& frame, int dataSz) {
        assert(state == TCPState::ESTABLISHED);
        assert(block.sndUNA <= block.sndNXT);
        assert(block.sndNXT > block.isn);
        assert(block.rcvNXT > block.irs);

        TCB ogBlock{block};

        const int packetSz = sizeof(PktType);

        prepareMsg(frame, packetSz, false, false);

        setTCPCsum(frame);

        ogBlock.sndNXT += dataSz;
        assert(ogBlock == block);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);
        assert(block.sndNXT > block.isn);
        assert(frame.dataSz() == 8);

        state = TCPState::ESTABLISHED;
    }

    [[nodiscard]] ErrorCode waitDataAck(const PacketHdr<u8[0]>* packet) const {
        assert(state == TCPState::ESTABLISHED);
        assert(packet->dataSz() == 0);
        return {
            .RECV_SYN = packet->tcp.syn == 1,
            .RECV_OLD_SEQ = block.rcvNXT != ntohl(packet->tcp.seq),
            .RECV_ACK = packet->tcp.ack != 1 || block.sndUNA > ntohl(packet->tcp.ack_seq),
            .RECV_WINDOW = ntohs(packet->tcp.window) < 128,
            .RECV_IS_RST = packet->tcp.fin == 1,
        };
    }


    [[nodiscard]] ErrorCode establishTCPConnection() {
        assert(state == TCPState::CLOSED);
        assert(io.socket.xskFD > 2);
        assert(io.qs.txQ.mask > 0);
        assert(io.qs.rxQ.mask > 0);
        assert(block.isn == block.sndNXT);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);

        prepareSyn();
        io.triggerWrite();
        assert(block.sndNXT == block.isn + 1);
        const auto synRcvHandler = [this](const PacketHdr<u8[0]>* packet) {
            assert(packet->tcp.fin == false);
            return synRecv(packet);
        };
        const ErrorCode err = waitAck<const PacketHdr<u8[0]>, true>(synRcvHandler);
        if (__builtin_expect(err.isErr(), false)) {
            return err;
        }
        prepareCompleteHandshake();
        io.triggerWrite();
        return {};
    }


    [[nodiscard]] u8* getOutputBuf(int pktSz) const {
        return io.getWriteBuff(pktSz);
    }

    template<typename PktType>
    [[nodiscard]] ErrorCode sendData(PktType& frame, int dataSz) {
        prepareDataSend(frame, dataSz);
        io.triggerWrite();

        const auto handler = [this](const PacketHdr<u8[0]>* packet) {
            return waitDataAck(packet);
        };
        const ErrorCode err = waitAck<PacketHdr<u8[0]>, false>(handler);

        io.complete();

        return err;
    }


    void close() {
        assert(state == TCPState::ESTABLISHED);
        assert(block.sndUNA <= block.sndNXT);
        assert(block.sndNXT > block.isn);
        assert(block.rcvNXT > block.irs);

        TCB ogBlock{block};

        using PktType = PacketHdr<u8[0]>;
        constexpr int packetSz = sizeof(PktType);

        u8* outputBuf = io.getWriteBuff(packetSz);

        PktType& frame = *reinterpret_cast<PktType *>(outputBuf);
        prepareMsg(frame, packetSz, false, true);
        frame.tcp.fin = true;

        setTCPCsum(frame);

        ++ogBlock.sndNXT;
        assert(ogBlock == block);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);
        assert(block.sndNXT > block.isn);

        state = TCPState::FIN1;
        io.triggerWrite();
        ogBlock.sndUNA = block.sndNXT;

        const auto stateUpd = [this](const PktType* pkt) {
            state = pkt->tcp.fin ? TCPState::FIN2 : state;
            block.rcvNXT += pkt->dataSz();
            block.rcvWND = ntohs(pkt->tcp.window);
            return ErrorCode{
                .RECV_ACK = pkt->tcp.fin && block.sndNXT != ntohl(pkt->tcp.ack_seq),
                .RECV_IS_RST = pkt->tcp.fin && ntohl(pkt->tcp.ack_seq) != block.sndNXT,
            };
        };

        // TODO set a timer here to wait on connection reset.
        while (state == TCPState::FIN1) {
            if (ErrorCode err = waitAck<PktType, true>(stateUpd); err.isErr()) {
                logErrAndExit(err);
            }
        }
        assert(block.sndUNA == block.sndNXT);
        assert(block.rcvNXT > ogBlock.rcvNXT);
        ogBlock.rcvNXT = block.rcvNXT;

        outputBuf = io.getWriteBuff(packetSz);

        frame = *reinterpret_cast<PktType *>(outputBuf);
        prepareMsg(frame, packetSz, false, true);
        frame.tcp.fin = true;

        setTCPCsum(frame);

        ogBlock.rcvWND = block.rcvWND;
        ++ogBlock.sndNXT;
        assert(ogBlock == block);
        io.triggerWrite();
        io.complete();
    }


    template<typename T>
    static void setTCPCsum(PacketHdr<T>& frame) {
        const u16 tcpLen = htons(frame.ip.tot_len) - sizeof(iphdr);
        const u16* tcpPkt = reinterpret_cast<u16 *>(&frame.tcp);
        frame.tcp.check = tcpudp_csum(frame.ip.saddr, frame.ip.daddr, tcpLen, IPPROTO_TCP, tcpPkt);
    }
};

#endif //TCP_H
