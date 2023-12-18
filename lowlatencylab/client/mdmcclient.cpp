//
// Created by jc on 04/11/23.
//

#include "mdmcclient.h"
#include "L2OB.h"
#include "OE.h"
#include "Strat.h"
#include <array>
#include <cassert>
#include <sched.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>


enum class StrategyState {
    INIT,
    OE_CONNECT,
    RUNNING
};

struct SampleMDPacket {
    MDFrame frame;
    MDPayload payload[5];
} __attribute__((packed));

class alignas(XSKUmem_FRAME_SIZE) Driver {
public:
    SampleMDPacket warmupPacket;
    StrategyState state = StrategyState::INIT;

    int fileTable[1]{};

    XDPIO io{"veth1", "/sys/fs/bpf/strat"};
    L2OB ob{};
    OE oe{io, "lll-1.oe"};
    Strat strat{oe, ob, io};

    Driver(): warmupPacket{} {
        static_assert(sizeof(MDFrame) == 42);
        static_assert(sizeof(MDPayload) == 40);
        constexpr int samplePacketSz = 242;
        static_assert(sizeof(SampleMDPacket) == samplePacketSz);
        u8 packetData[samplePacketSz] = {
            oe.tcp.cfg.destMAC[0], oe.tcp.cfg.destMAC[1], oe.tcp.cfg.destMAC[2], oe.tcp.cfg.destMAC[3],
            oe.tcp.cfg.destMAC[4], oe.tcp.cfg.destMAC[5],
            oe.tcp.cfg.srcMAC[0], oe.tcp.cfg.srcMAC[1], oe.tcp.cfg.srcMAC[2], oe.tcp.cfg.srcMAC[3],
            oe.tcp.cfg.srcMAC[4], oe.tcp.cfg.srcMAC[5],
            0x08, 0x00,
            0x45, 0x00, 0x00, 0xe4, 0x3c, 0x8e, 0x40, 0x00, 0x40, 0x11, 0xe9, 0x78, 0x0a, 0x00, 0x00, 0x02,
            0x0a, 0x00, 0x00, 0x01, 0xd2, 0xda, 0x10, 0xe1, 0x00, 0xd0, 0x14, 0xe4, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x22, 0x80, 0xff, 0x95, 0xf3,
            0x9e, 0x17, 0x20, 0x85, 0x13, 0x54, 0x02, 0x00, 0x00, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x77, 0x22, 0x80, 0xff, 0x95, 0xf3, 0x9e, 0x17, 0x00, 0xe4, 0x0b, 0x54, 0x02, 0x00,
            0x00, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x22, 0x80, 0xff, 0x95, 0xf3,
            0x9e, 0x17, 0xa0, 0x6a, 0x0d, 0x54, 0x02, 0x00, 0x00, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x01, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x77, 0x22, 0x80, 0xff, 0x95, 0xf3, 0x9e, 0x17, 0x80, 0xfe, 0x11, 0x54, 0x02, 0x00,
            0x00, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x22, 0x80, 0xff, 0x95, 0xf3,
            0x9e, 0x17, 0x40, 0xf1, 0x0e, 0x54, 0x02, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x76, 0x48, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        copy_n(packetData, samplePacketSz, reinterpret_cast<u8 *>(&warmupPacket));
    }

    void run() {
        while (__builtin_expect(!strat.isComplete, true)) {
            assert(stateCheck());
            switch (state) {
                case StrategyState::INIT: {
                    assert(!oe.isConnected());

                    state = StrategyState::OE_CONNECT;
                    break;
                }
                case StrategyState::OE_CONNECT: {
                    assert(!oe.isConnected());

                    oe.establishConnection();

                    assert(oe.isConnected());
                    assert(strat.lastReceivedNs == 0);
                    assert(strat.cursor == 0);
                    assert(ob.seen.empty());

                    state = StrategyState::RUNNING;
                    break;
                }
                case StrategyState::RUNNING: {
                    assert(oe.isConnected());

                    static u64 prevCheckpoint = currentTimeNs();
                    static double prevTimeSpent = timeSpent[0];
                    static int counter1 = 0;
                    io.qs.stateCheck();
                    io.umem.stateCheck();
                    io.socket.stateCheck();

                    const auto& handler = [this](const u8* inBuf, u32 len, bool isAvail) {
                        if (__builtin_expect(isAvail, true)) {
                            const PacketHdr<u8[0]>* packet = reinterpret_cast<const PacketHdr<u8[0]> *>(inBuf);

                            assert(packet->eth.h_proto == htons(ETH_P_IP));
                            assert(packet->ip.version == 4);
                            assert(packet->ip.ihl == 5);
                            assert(htons(packet->ip.tot_len) == len - sizeof(ethhdr));
                            assert((ntohs(packet->ip.frag_off) & 0x1fff) == 0);
                            assert(((packet->ip.frag_off >> 13) & 1) == 0);
                            assert(((packet->ip.frag_off >> 15) & 1) == 0);
                            if (packet->ip.protocol == IPPROTO_UDP) {
                                return strat.recvUdpMD<true>(inBuf, len);
                            }
                            if (packet->ip.protocol == IPPROTO_TCP) {
                                assert(isAvail);
                                const auto& handler = [this](const PacketHdr<u8[0]>* packet) {
                                    return this->oe.tcp.waitDataAck(packet);
                                };
                                return oe.tcp.processFrame<PacketHdr<u8[0]>>(handler, packet, len);
                            }
                            cerr << "Unexpected protocol [" << packet->ip.protocol << "]" << endl;
                            return ErrorCode{.IP_PROTO = 1};
                        } else {
                            inBuf = reinterpret_cast<const u8 *>(&warmupPacket);
                            len = sizeof(warmupPacket);
                            return strat.recvUdpMD<false>(inBuf, len);
                        }
                    };
                    CLOCK(TOT_RECV_PC,
                          u32 idx;
                          u32 available;
                          u32 fillQIdx;
                          u32 reserved;
                          // if we get multiple out of order packets then we should process them smartly - last to first.
                          CLOCK(SYS_RECV_PC,
                              const auto& res = io.recv<true>();
                              available = res.available;
                              reserved = res.reserved;
                              idx = res.idx;
                              fillQIdx = res.fillQIdx;
                              assert(reserved == available);
                          )
                          if(const auto& err = io.handleFrames(available, reserved, idx, fillQIdx, handler);
                              __builtin_expect(err.isErr(), false)) {
                          logErrAndExit(err);
                          }
                    )
                    constexpr int modulus = 0x0;
                    if (__builtin_expect((++counter1 & modulus) == 0, false)) {
                        const TimeNs cTime = currentTimeNs();
                        cout << "Iters [" << counter1 << "]" << '\n';
                        cout << "Prev Avg Loop Time [" << (cTime - prevCheckpoint) / 1'000.0 / (modulus + 1) << "us]" <<
                                '\n';
                        cout << "Prev Time Spend [" << (GET_PC(0) - prevTimeSpent) * 1'000'000.0 / (modulus + 1) <<
                                "us]" << '\n';
                        cout << "Total Packet Proc [" << GET_PC(0) * 1'000'000.0 / counter1 << "us]" << '\n';
                        cout << "Book update [" << GET_PC(1) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "Order Submission [" << GET_PC(2) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "Message Handling [" << GET_PC(3) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "Recv [" << GET_PC(4) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "----------------------" << '\n';
                        prevTimeSpent = timeSpent[0];
                        prevCheckpoint = cTime;
                    }
                    //                    assert(std::abs(currentTimeNs() - strat.lastReceivedNs) < 10'000'000);


                    assert(strat.isConnected() || strat.isComplete);
                    break;
                }
                default: {
                    assert(false);
                }
            }
        }
        cout << "Done" << endl;
    }

    bool stateCheck() const {
        assert(strat.cursor >= 0);
        assert(strat.cursor == 0 || strat.isConnected());
        assert(strat.cursor == 0 || oe.isConnected());
        assert(strat.cursor == 0 || strat.lastReceivedNs > 0);
        switch (state) {
            case StrategyState::INIT: {
                assert(!oe.isConnected());
                assert(strat.cursor == 0);
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
                break;
            }
            default: {
                assert(false);
            }
        }
        return true;
    }
};

int main() {
    const auto numCores = sysconf(_SC_NPROCESSORS_ONLN);
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

    sched_param schparam{};
    constexpr int receiveThreadPolicy = SCHED_FIFO;
    constexpr int priority = 99; // sched_get_priority_max(receiveThreadPolicy);
    schparam.sched_priority = priority;
    ret = sched_setscheduler(0, receiveThreadPolicy, &schparam);

    if (ret) {
        cerr << "Error [" << errno << " in setting priority: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }

    Driver s{};
    s.run();
}
