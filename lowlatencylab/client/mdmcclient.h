#ifndef MDMCCLIENT_H
#define MDMCCLIENT_H

#include "OE.h"
#include "defs.h"
#include "IGB82576IO.h"
#include "Strat.h"
#include "TCPStack.h"
#include "L2OB.h"
#include "../../cppkern/ucontext_.h"


enum class StrategyState {
    INIT,
    OE_CONNECT,
    RUNNING
};

class alignas(IGBConfig::FRAME_SIZE) Driver {
public:
    StrategyState state = StrategyState::INIT;

    IGB82576IO<Driver, TCP<Driver>> io;
    L2OB ob;
    OE<Driver> oe;
    Strat<Driver> strat;
    IOBlockers<Driver, TCP<Driver>> blocker;

    Driver(void* adapter, void* rx, ContextMgr& ctxM): io{adapter, rx, ctxM}, ob{}, oe{io}, strat{oe, ob},
	blocker{.fst = this} {
        assert(sizeof(MDFrame) == 42);
        static_assert(sizeof(MDPayload) == 40);
    }


    void run() {
	    pr_info__("Commencing strategy loop");

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

                    state = StrategyState::RUNNING;
                    break;
                }
                case StrategyState::RUNNING: {
                    assert(oe.isConnected());

                    static u64 prevCheckpoint = currentTimeNs();
                    static double prevTimeSpent = timeSpent[0];
                    static int counter1 = 0;

                    io.stateCheck();

                    // CLOCK(TOT_RECV_PC,
                    // if we get multiple out of order packets then we should process them smartly - last to first.
                    // CLOCK(SYS_RECV_PC,
                    io.recv(blocker, packetRouter);
                    // )
                    constexpr int modulus = 0x0;
                    if (__builtin_expect((++counter1 & modulus) == 0, false)) {
                        const TimeNs cTime = currentTimeNs();
                        printf__("Iters [%d]", counter1);
                        printf__("Prev Avg Loop Time [%d]ns.", (cTime - prevCheckpoint) / (modulus + 1));
                        printf__("Prev Time Spend [%d]us.", (GET_PC(0) - prevTimeSpent) * 1'000'000 / (modulus + 1));
                        printf__("Total Packet Proc [%d].", GET_PC(0) * 1'000'000.0 / counter1);
                        printf__("Book update [%d].", GET_PC(1) * 100/ GET_PC(0) );
                        printf__("Order Submission [%d].", GET_PC(2) *100 / GET_PC(0));
                        printf__("Message Handling [%d].", GET_PC(3) *100 / GET_PC(0));;
                        printf__("Recv [%d].", GET_PC(4) * 100 / GET_PC(0));
                        printf__("----------------------");
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
	oe.tcp.close();
        // TODO - after this is done, we dont want this to terminate... what do we do?
        printf__("Done\n");
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
                break;
            }
            case StrategyState::OE_CONNECT: {
                assert(strat.lastReceivedNs <= 0 || strat.isConnected());
                assert(!oe.isConnected());
                assert(strat.cursor == 0);
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


    static ErrorCode packetRouter(IOBlockers<Driver, TCP<Driver>>& blocker, const u8* inBuf, u32 len) {
    	Driver& driver = *blocker.fst;

    	const PacketHdr<u8[0]>* packet = reinterpret_cast<const PacketHdr<u8[0]> *>(inBuf);

    	// assert(packet->eth.h_proto == htons__(ETH_P_IP));
    	// assert(packet->ip.version == 4);
    	// assert(packet->ip.ihl == 5);
    	// assert(htons__(packet->ip.tot_len) == len - sizeof(ethhdr));
    	// assert((ntohs__(packet->ip.frag_off) & 0x1fff) == 0);
    	// assert(((packet->ip.frag_off >> 13) & 1) == 0);
    	// assert(((packet->ip.frag_off >> 15) & 1) == 0);
    	if (packet->ip.protocol == IPPROTO_UDP) {
    		return driver.strat.recvUdpMD(inBuf, len);
    	}
    	if (packet->ip.protocol == IPPROTO_TCP) {
    		const auto& handler = [&driver](const PacketHdr<u8[0]>* packet) {
    			return driver.oe.tcp.waitDataAck(packet);
    		};
    		return driver.oe.tcp.processFrame<PacketHdr<u8[0]>>(handler, packet, len);
    	}
    	printf__("Unexpected protocol [%d]\n", packet->ip.protocol);
    	return ErrorCode{.IP_PROTO = 1};
    }

};

#endif
