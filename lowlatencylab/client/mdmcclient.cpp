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


class Driver {


    StrategyState state = StrategyState::INIT;

    IOUringState ioState{};
    int fileTable[1]{};

    L2OB ob{};
    OE oe{ioState};
    Strat strat{oe, ob};

public:


    void run() {
        while (__builtin_expect(!strat.isComplete, true)) {
            assert(stateCheck());
            switch (state) {
                case StrategyState::INIT: {
                    assert(strat.mdFD != -1);
                    assert(!oe.isConnected());
                    assert(fileTable[0] != strat.mdFD);
                    fileTable[0] = strat.mdFD;
                    io_uring_register_files(&ioState.ring, fileTable, 1);

                    state = StrategyState::OE_CONNECT;
                    break;
                }
                case StrategyState::OE_CONNECT: {
                    assert(strat.mdFD != -1);
                    assert(!oe.isConnected());
                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(io_uring_cq_ready(&ioState.ring) == 0);

                    oe.establishConnection();

                    assert(oe.isConnected());
                    assert(strat.lastReceivedNs == 0);
                    assert(strat.cursor == 0);
                    assert(!strat.isConnected());
                    assert(ob.seen.empty());

                    state = StrategyState::RUNNING;
                    break;
                }
                case StrategyState::RUNNING: {
                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(!io_uring_cq_has_overflow(&ioState.ring));
                    assert(oe.isConnected());

                    static u64 prevCheckpoint = currentTimeNs();
                    static double prevTimeSpent = timeSpent[0];
                    static int counter1 = 0;
                    CLOCK(TOT_RECV_PC,
                            strat.recvUdpMD();
                    )
                    int modulus = 0x1fff;
//                    if(__builtin_expect((++counter1 & modulus) == 0, false)) {
                    if((++counter1 & modulus) == 0) {
                        TimeNs cTime = currentTimeNs();
                        cout << "Iters [" << counter1 << "]" << '\n';
                        cout << "Prev Avg Loop Time [" << (cTime - prevCheckpoint) / 1'000.0 / (modulus + 1) << "us]" << '\n';
                        cout << "Prev Time Spend [" << (GET_PC(0) - prevTimeSpent) * 1'000'000.0 / (modulus + 1) << "us]" << '\n';
                        cout << "Total Packet Proc [" << GET_PC(0) * 1'000'000.0 / counter1 << "us]" << '\n';
                        cout << "Book update [" << GET_PC(1) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "Order Submission [" << GET_PC(2) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "Message Handling [" << GET_PC(3) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "Recv [" << GET_PC(4) / GET_PC(0) * 100 << "%]" << '\n';
                        cout << "----------------------" << '\n';
                        prevTimeSpent = timeSpent[0];
                        prevCheckpoint = cTime;
                    }
                    assert(std::abs(currentTimeNs() - strat.lastReceivedNs) < 1'000'000);

                    if (u32 ready = io_uring_cq_ready(&ioState.ring)) {
                        unsigned head;
                        unsigned processed = 0;
                        struct io_uring_cqe *cqe;
                        io_uring_for_each_cqe(&ioState.ring, head, cqe) {
                            assert(nullptr != cqe);
#pragma clang diagnostic push
#pragma ide diagnostic ignored "NullDereference"
                            io_uring_cqe &e = *cqe;
#pragma clang diagnostic pop
                            u64 userData = io_uring_cqe_get_data64(&e);
                            if (userData >= OE::ORDER_TAG) {
                                oe.completeMessage(e);
                            } else {
                                assert(false);
                            }
                            ++processed;
                        }
                        assert(processed >= ready);
                        io_uring_cq_advance(&ioState.ring, processed);
                    }

                    assert(strat.isConnected() || !strat.isComplete);
                    break;
                }
                default: {
                    assert(false);
                }

            }
        }
        cout << "Done" << endl;
    }

    bool stateCheck() {
        assert(strat.cursor >= 0);
        assert(strat.cursor == 0 || oe.isConnected() && strat.isConnected() && strat.lastReceivedNs > 0);
        assert(ioState.ring.sq.ring_entries >= 256);
        assert(ioState.ring.ring_fd > 2);
        switch (state) {
            case StrategyState::INIT: {
                assert(!oe.isConnected());
                assert(!strat.isConnected());
                assert(strat.cursor == 0);
                assert(ob.seen.empty());
                assert(io_uring_sq_ready(&ioState.ring) == 0);
                assert(io_uring_cq_ready(&ioState.ring) == 0);
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

    auto numCores = sysconf(_SC_NPROCESSORS_ONLN);
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

    Driver s{};
    s.run();
}