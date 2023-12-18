//
// Created by joshuacoutinho on 03/12/23.
//

#ifndef TCP_H
#define TCP_H

#include "../../cppkern/ucontext_.h"
#include "defs.h"
#include "IGB82576IO.h"

using namespace std;

struct TCPConnConfig {
    const u8 destMAC[6]{};
    const u8 srcMAC[6]{};
    const u8 sourceIP[4]{};
    const u8 destIP[4]{};
    const u16 sourcePort{};
    const u16 destPort{};
};


enum class TCPState {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN1,
    FIN2
};

using TCPSeqNo = u32;
using SegmentSz = u32;

struct TCB {
    constexpr static u32 RCV_WINDOW_SZ = 1 << 15;
    constexpr static u32 SND_WINDOW_SZ = 1 << 14;

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
    // TODO - need to sync up send window with slots on the tx ring.
    u32 sndWND{SND_WINDOW_SZ};
    u32 sndWL1{};
    u32 sndWL2{};
    u32 isn{};

    u32 rcvNXT{};
    u32 rcvWND{RCV_WINDOW_SZ};
    u32 irs{};

    u32 ipMsgId{0};
    TCPState state{TCPState::CLOSED};
    SegmentSz peerMSS{536};
    SegmentSz ourMSS{536};
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
           b1.irs == b2.irs &&
           b1.state == b2.state &&
           b1.peerMSS == b2.peerMSS &&
           b1.ourMSS == b2.ourMSS &&
           b1.ipMsgId == b2.ipMsgId;
}


template<typename Driver>
// TODO rto
class TCP {
public:
    const TCPConnConfig cfg;

    IGB82576IO<Driver, TCP>& io;

    TCB block;
    IOBlockers<Driver, TCP> blocker;


    TCP(IGB82576IO<Driver, TCP>& io, const TCPConnConfig cfg): cfg{cfg}, io{io}, blocker{.snd = this} {
        assert(sizeof(ErrorCode) == 1);
        block.isn = isnGen();
        block.sndNXT = block.isn;
    }

    static TCPSeqNo isnGen() {
        const u64 timeNowNs = currentTimeNs();
        return static_cast<TCPSeqNo>(timeNowNs / 4'000);
    }


    template<typename T>
    void parseTCPOptions(const PacketHdr<T>* packet) {
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
                block.peerMSS = ntohs__(*reinterpret_cast<const u16 *>(optionByte + 2));
                pr_info__("Peer MSS [%d].\n", block.peerMSS);
                optionsOffs += 4;
            } else {
                const int tcpOptionLen = *(optionByte + 1);
                pr_info__("TCP Option [%d %d].", *optionByte, tcpOptionLen);;
                optionsOffs += tcpOptionLen;
            }
            assert(optionsOffs <= optionsSz);
        }
        for (const u8* i = optionsOffsPtr + optionsOffs; i < optionsOffsPtr + optionsSz; ++i) {
            assert(*i == 0);
        }
    }

    void copy(const u8* src, const u8* end, u8* dst) {
        while (src != end) {
            *dst++ = *src++;
        }
    }

    template<typename T>
    void prepareMsg(PacketHdr<T>& frame, int packetSz, bool isSyn, bool isFin) {
        copy(cfg.srcMAC, cfg.srcMAC + ETH_ALEN, frame.eth.h_source);
        copy(cfg.destMAC, cfg.destMAC + ETH_ALEN, frame.eth.h_dest);

        frame.eth.h_proto = htons__(ETH_P_IP);
        frame.ip.ihl = 5;
        frame.ip.version = 4;
        frame.ip.tos = 0;
        frame.ip.tot_len = htons__(packetSz - sizeof(ethhdr));
        frame.ip.id = block.ipMsgId;
        frame.ip.frag_off = 0x0;
        frame.ip.ttl = IPDEFTTL;
        frame.ip.protocol = IPPROTO_TCP;
        frame.ip.check = 0;
        frame.ip.saddr = *reinterpret_cast<const u32 *>(cfg.sourceIP);
        frame.ip.daddr = *reinterpret_cast<const u32 *>(cfg.destIP);
        const u8* dataptr = reinterpret_cast<u8 *>(&frame.ip);
        const u16 kernelcsum = ip_fast_csum(dataptr, frame.ip.ihl);
        frame.ip.check = kernelcsum;

        frame.tcp.source = htons__(cfg.sourcePort);
        frame.tcp.dest = htons__(cfg.destPort);
        frame.tcp.seq = htonl__(block.sndNXT);
        frame.tcp.ack_seq = htonl__(block.rcvNXT);
        assert(sizeof(tcphdr) % 4 == 0);
        frame.tcp.doff = sizeof(tcphdr) / 4;
        frame.tcp.ece = 0;
        frame.tcp.cwr = 0;
        frame.tcp.res1 = 0;
        frame.tcp.urg = 0;
        frame.tcp.ack = 1;
        frame.tcp.psh = 0;
        frame.tcp.rst = 0;
        frame.tcp.syn = isSyn;
        frame.tcp.fin = isFin;
        frame.tcp.window = htons__(TCB::RCV_WINDOW_SZ);
        frame.tcp.check = 0;
        frame.tcp.urg_ptr = 0;

        block.sndNXT += max(static_cast<u32>(sizeof(T)), static_cast<u32>(isSyn) | static_cast<u32>(isFin));
        ++block.ipMsgId;

        assert(frame.ip.check != 0);
        assert(frame.eth.h_proto == htons__(ETH_P_IP));
        assert(frame.ip.frag_off == 0);
        assert(frame.ip.ttl != 0);
        assert(frame.ip.protocol == IPPROTO_TCP);
        assert(frame.ip.tot_len == htons__(packetSz - sizeof(ethhdr)));
    }


    PacketHdr<u8[0]>& prepareSyn() {
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

        block.state = TCPState::SYN_SENT;
        return frame;
    }

    template<typename T>
    [[nodiscard]] ErrorCode rcvPktCheck(const PacketHdr<T>* packet, u32 bytesReadWithPhy) {
        const u32 ackSeq = ntohl__(packet->tcp.ack_seq);
        const u32 senderSeq = ntohl__(packet->tcp.seq);

        const ErrorCode err{
            .TCP_RECV_OLD_SEQ = senderSeq < block.rcvNXT,
            .TCP_RECV_ACK = static_cast<u32>(ackSeq) < block.sndUNA,
            .TCP_RECV_IS_RST = packet->tcp.rst != 0 || (block.state != TCPState::FIN1 && packet->tcp.fin),
            .TCP_RECV_UNEXPECTED_ACK = packet->tcp.ack != 1
        };
        if (__builtin_expect(err.isErr(), false)) {
            return err;
        }

        assert(packet->eth.h_proto == htons__(ETH_P_IP));
        assert(packet->ip.version == 4);
        assert(packet->ip.ihl == 5);
        assert(
            ntohs__(packet->ip.tot_len) <= bytesReadWithPhy - sizeof(ethhdr) && ntohs__(packet->ip.tot_len) + 8 >=
            bytesReadWithPhy - sizeof(ethhdr));
        assert((ntohs__(packet->ip.frag_off) & 0x1fff) == 0);
        assert(((packet->ip.frag_off >> 13) & 1) == 0);
        assert(packet->tcp.source == htons__(OE_PORT));
        assert(packet->tcp.dest == htons__(TCP_SENDING_PORT));
        assert(packet->tcp.doff <= 10);
        assert(packet->tcp.res1 == 0);
        assert(packet->tcp.urg == 0);
        assert(ntohs__(packet->tcp.window) >= 128 && ntohs__(packet->tcp.window) <= 65536);
        assert(packet->tcp.urg_ptr == 0);


        return err;
    }


    template<typename PktType, typename RcvHandler>
    [[nodiscard]] ErrorCode processFrame(const RcvHandler& handler, const PktType* packet, u32 bytesReadWithPhy) {
        assert(bytesReadWithPhy > 0);
        assert(bytesReadWithPhy - sizeof(ethhdr) - sizeof(iphdr) <= block.sndWND);

        if (const ErrorCode err = rcvPktCheck(packet, bytesReadWithPhy); err.isErr()) {
            return err;
        }
        parseTCPOptions(packet);
        u32 oneByteAck = static_cast<u32>(packet->tcp.syn) | static_cast<u32>(packet->tcp.fin);
        block.rcvNXT = ntohl__(packet->tcp.seq) + max(packet->dataSz(), oneByteAck);
        block.rcvWND = ntohs__(packet->tcp.window);
        block.sndUNA = ntohl__(packet->tcp.ack_seq);
        return handler(packet);
    }

    template<typename RcvHandler>
    [[nodiscard]] ErrorCode waitAck(const RcvHandler& handler, int maxExpectedFrames) {
        io.stateCheck();

        // if we get multiple out of order packets then we should process them smartly - last to first.
        // TODO - get the return code here - make swapcontext take a return value as well.
        io.recv(blocker, handler);
        return {};
    }

    [[nodiscard]] ErrorCode synRecv(const PacketHdr<u8[0]>* packet) {
        assert(block.sndUNA == block.sndNXT);
        assert(block.state == TCPState::SYN_SENT);
        assert(block.ipMsgId == 1);

        block.irs = ntohl__(packet->tcp.seq);
        block.rcvNXT = ntohl__(packet->tcp.seq) + 1;
        block.sndUNA = block.sndNXT;

        assert(block.sndUNA == block.sndNXT);
        assert(block.irs == ntohl__(packet->tcp.seq));
        assert(block.rcvWND == ntohs__(packet->tcp.window));
        assert(block.peerMSS >= 500 && block.peerMSS <= 1500);
        assert(block.rcvWND >= 1024 && block.rcvWND <= 1 << 16);

        return {
            .TCP_RECV_SYN = packet->tcp.syn != 1,
            .TCP_RECV_OLD_SEQ = block.rcvNXT != ntohl__(packet->tcp.seq) + 1,
        };
    }

    PacketHdr<u8[0]>& prepareCompleteHandshake() {
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

        block.state = TCPState::ESTABLISHED;

        return frame;
    }

    template<typename PktType>
    void prepareDataSend(PktType& frame, int dataSz) {
        assert(block.state == TCPState::ESTABLISHED);
        assert(block.sndUNA <= block.sndNXT);
        assert(block.sndNXT > block.isn);
        assert(block.rcvNXT > block.irs);

        TCB ogBlock{block};

        const int packetSz = sizeof(PktType);

        prepareMsg(frame, packetSz, false, false);

        setTCPCsum(frame);

        ogBlock.sndNXT += dataSz;
        ++ogBlock.ipMsgId;
        assert(ogBlock == block);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);
        assert(block.sndNXT > block.isn);

        block.state = TCPState::ESTABLISHED;
    }

    [[nodiscard]] ErrorCode waitDataAck(const PacketHdr<u8[0]>* packet) const {
        assert(block.state == TCPState::ESTABLISHED);
        assert(packet->dataSz() == 0);
        return {
            .TCP_RECV_SYN = packet->tcp.syn == 1,
            .TCP_RECV_OLD_SEQ = block.rcvNXT != ntohl__(packet->tcp.seq),
            .TCP_RECV_ACK = packet->tcp.ack != 1 || block.sndUNA > ntohl__(packet->tcp.ack_seq),
            .TCP_RECV_WINDOW = ntohs__(packet->tcp.window) < 128,
            .TCP_RECV_IS_RST = packet->tcp.fin == 1,
        };
    }

    static ErrorCode synRcvHandler(IOBlockers<Driver, TCP>& blockers, const u8* inBuf, u32 len) {
        pr_info__("Syn recv handler");
        TCP* tcp = blockers.snd;
        const auto* packet = reinterpret_cast<const PacketHdr<u8[0]> *>(inBuf);
        assert(packet->tcp.fin == false);
        return tcp->processFrame([tcp](const auto* packet) { return tcp->synRecv(packet); }, packet, len);
    };


    [[nodiscard]] ErrorCode establishTCPConnection() {
        assert(block.state == TCPState::CLOSED);
        assert(block.isn == block.sndNXT);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);

        io.triggerWrite(prepareSyn());
        assert(block.sndNXT == block.isn + 1);


        if (const ErrorCode err = waitAck(synRcvHandler, 1); err.isErr()) {
            return err;
        }

        const PacketHdr<u8[0]>& pkt = prepareCompleteHandshake();

        io.triggerWrite(pkt);

        return {};
    }


    [[nodiscard]] u8* getOutputBuf(int pktSz) const {
        return io.getWriteBuff(pktSz);
    }

    template<typename DataT>
    [[nodiscard]] ErrorCode sendData(PacketHdr<DataT>& frame) {
        constexpr int dataSz = sizeof(DataT);
        TCB ogBlock{block};
        prepareDataSend(frame, dataSz);

        // CLOCK(ORDER_SUBMISSION_PC,
        io.triggerWrite(frame);
        // )
        io.complete();

        return {};
    }

    static ErrorCode finHandler(IOBlockers<Driver, TCP>& blocker, const u8* inBuf, u32 _len) {
        using PktType = PacketHdr<u8[0]>;
        TCP* tcp = blocker.snd;
        const auto stateUpd = [tcp](const PktType* pkt) {
            tcp->block.state = pkt->tcp.fin ? TCPState::FIN2 : tcp->block.state;
            tcp->block.rcvNXT += pkt->dataSz();
            tcp->block.rcvWND = ntohs__(pkt->tcp.window);
            return ErrorCode{
                .TCP_RECV_ACK = pkt->tcp.fin && tcp->block.sndNXT != ntohl__(pkt->tcp.ack_seq),
                .TCP_RECV_IS_RST = pkt->tcp.fin && ntohl__(pkt->tcp.ack_seq) != tcp->block.sndNXT,
            };
        };

        assert(_len == sizeof(ethhdr) + sizeof(iphdr) + sizeof(tcphdr));
        const auto* pkt = reinterpret_cast<const PktType *>(inBuf);
        return tcp->processFrame(stateUpd, pkt, _len);
    };


    void close() {
        assert(block.state == TCPState::ESTABLISHED);
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
        ++ogBlock.ipMsgId;
        assert(ogBlock == block);
        assert(block.sndWND == TCB::SND_WINDOW_SZ);
        assert(block.sndNXT > block.isn);

        block.state = TCPState::FIN1;
        io.triggerWrite(frame);
        ogBlock.sndUNA = block.sndNXT;


        // TODO set a timer here to wait on connection reset.
        while (block.state == TCPState::FIN1) {
            if (ErrorCode err = waitAck(finHandler, IGBConfig::NUM_READ_DESC); err.isErr()) {
                logErrAndExit(err);
            }
        }
        assert(block.sndUNA == block.sndNXT);
        assert(block.rcvNXT > ogBlock.rcvNXT);
        /* TODO - this isn't generating the right message
        ogBlock.rcvNXT = block.rcvNXT;
        {
            u8* outputBuf = io.getWriteBuff(packetSz);

            auto& frame = *reinterpret_cast<PktType *>(outputBuf);
            prepareMsg(frame, packetSz, false, true);

            setTCPCsum(frame);

            ogBlock.rcvWND = block.rcvWND;
            ++ogBlock.sndNXT;
            assert(ogBlock == block);
            io.triggerWrite();
            io.complete();
        }
        */
    }


    template<typename T>
    static void setTCPCsum(PacketHdr<T>& frame) {
        // TODO - use hardware offloading for csum calculation.
        const u16 tcpLen = htons__(frame.ip.tot_len) - sizeof(iphdr);
        const u16* tcpPkt = reinterpret_cast<u16 *>(&frame.tcp);
        frame.tcp.check = tcpudp_csum(frame.ip.saddr, frame.ip.daddr, tcpLen, IPPROTO_TCP, tcpPkt);
    }
};

#endif //TCP_H
