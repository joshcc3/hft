//
// Created by jc on 04/11/23.
//

#include "../defs.h"
#include "OEServer.h"
#include "MDServer.h"

#include <iostream>
#include <fstream>
#include <mutex>
#include <cassert>
#include <cstdlib>
#include <liburing.h>
#include <unistd.h>
#include <sys/socket.h>
#include <boost/iostreams/filter/gzip.hpp>


// Assuming we have these namespaces
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::size_t;


// A simple State enum for Exchange state management.
enum class ExchangeState {
    INIT, HANDSHAKE, TRANSMIT
};

class Exchange {
    ExchangeState state;
    IOUringState ioState;
    MDServer md;
    OEServer oe;


public:
    Exchange(std::ifstream &f, const string &headerLine, const string &outputFileN)
            : state(ExchangeState::INIT), ioState(), oe{ioState, md, outputFileN},
              md{ioState, f, headerLine} {
        assert(io_uring_sq_ready(&ioState.ring) == 0);
    }

    [[noreturn]] void run() {
        while (true) {
            switch (state) {
                case ExchangeState::INIT: {
                    oe.doAccept();

                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(io_uring_cq_ready(&ioState.ring) == 0);
                    cout << "OE Connected" << endl;
                    state = ExchangeState::HANDSHAKE;
                    break;
                }
                case ExchangeState::HANDSHAKE: {
                    oe.prepareRecv();
                    assert(io_uring_sq_ready(&ioState.ring) == 2);
                    md.prepBuffer();
                    assert(io_uring_sq_ready(&ioState.ring) == 3);
                    int submittedEvents = ioState.submit();
                    assert(submittedEvents == 3);

                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(io_uring_cq_ready(&ioState.ring) >= 0);

                    cout << "MD Transmitting" << endl;
                    state = ExchangeState::TRANSMIT;

                    break;
                }
                case ExchangeState::TRANSMIT: {
                    // check for received messages from order entry and enqueue more reads
                    // write out marketdata

                    if (!oe.connectionAlive() && !md.isAlive()) {
                        md.reset();
                        assert(io_uring_sq_ready(&ioState.ring) == 0);
                        if (io_uring_cq_ready(&ioState.ring)) {
                            cqe_guard g{ioState};
                            [[maybe_unused]] __u64 d = g.completion->user_data;
                            [[maybe_unused]] __s32 r = g.completion->res;
                            assert(false);
                        }
                        state = ExchangeState::INIT;
                    } else {
                        assert(!io_uring_cq_has_overflow(&ioState.ring));
                        assert(io_uring_sq_ready(&ioState.ring) == 0);

                        while (unsigned int ready = io_uring_cq_ready(&ioState.ring)) {

                            struct io_uring_cqe *cqe;
                            unsigned head;
                            unsigned cqeProc = 0;

                            io_uring_for_each_cqe(&ioState.ring, head, cqe) {
                                assert(!io_uring_cq_has_overflow(&ioState.ring));
                                io_uring_cqe &e = *cqe;
                                u64 userData = e.user_data;
                                int resultCode = e.res;

                                if (userData == OEServer::RECV_TAG) {
                                    assert(oe.isAlive);
                                    oe.completeMessages(e);
                                } else if (userData >= MDServer::MD_SEND_TAG &&
                                           userData <= MDServer::MD_SEND_TAG + md.sentBytes) {
                                    assert((e.flags & IORING_CQE_F_MORE) == 0);
                                    assert((e.flags & IORING_CQE_F_BUFFER) == 0);
                                    if (resultCode > 0) {
                                        md.completeMessage(userData, resultCode);
                                    } else if (-resultCode == EAGAIN || -resultCode == EALREADY) {
                                        cerr << "OS Throttle [" << strerror(-resultCode) << "]. " << endl;
                                        md.throttle(userData);
                                    } else {
                                        assert(false);
                                    }
                                } else if (userData == OEServer::BUFFER_ADD_TAG) {
                                    [[maybe_unused]] u64 completionRes = e.res;
                                    [[maybe_unused]] u64 completionTag = e.user_data;
                                    assert(e.res == OEServer::NUM_BUFFERS || e.res == 0);
                                    assert(!oe.used.test(6));
                                } else if (userData == OEServer::BUFFER_REMOVE_TAG) {
                                    auto completionRes = e.res;
                                    if (completionRes <= 0) {
                                        cerr << "Failed to dealloc buffers [" << completionRes << "] [" << userData
                                             << "]." << endl;
                                        assert(false);
                                    }
                                    oe.used.reset();
                                } else {
                                    assert(false);
                                }

                                cqeProc++;
                            }
                            assert(cqeProc >= ready);

                            io_uring_cq_advance(&ioState.ring, cqeProc);

                        }

                        unsigned int pending = io_uring_sq_ready(&ioState.ring);
                        assert(pending <= 2);
                        if (!md.isEOF() && oe.connectionAlive()) {
                            md.send();
                        }
                    }


                    break;
                }
            }
            stateCheck();

        }
    }



    // Other related methods...
private:

    bool stateCheck() {

        assert(ioState == oe.ioState);
        assert(ioState == md.ioState);
        assert(io_uring_sq_space_left(&ioState.ring) > 0);

        assert(oe.isAlive == (oe.clientFD != -1));
        assert(md.sentBytes >= md.ackedBytes);
        assert(md.sentBytes < (1 << 10) || md.sentBytes * 0.1 <= md.ackedBytes ||
               md.sentBytes - md.ackedBytes < (MDServer::SND_BUF_SZ / 2));
        assert(md.cursor * 10 <= md.sentBytes);

        switch (state) {
            case ExchangeState::INIT: {
                assert(io_uring_cq_ready(&ioState.ring) == 0);

                assert(ioState.ring.sq.sqe_tail - ioState.ring.sq.sqe_head == 0);

                assert(oe.clientFD == -1);
                assert(oe.connectionSeen == 0 || oe.serverFD != -1);
                assert(oe.curLabResult.empty());

                assert(oe.connectionSeen == 0 || md.serverFD != -1);
                assert(md.cursor == 0);
                md.instream->peek();
                assert(!md.instream->fail());

                break;
            }
            case ExchangeState::HANDSHAKE: {
                assert(ioState.ring.sq.sqe_tail - ioState.ring.sq.sqe_head <= 1);

                oeHandshakeState();

                assert(oe.connectionSeen == 1 || md.serverFD != -1);
                assert(md.cursor == 0);

                break;
            }
            case ExchangeState::TRANSMIT: {
                assert(ioState.ring.sq.sqe_tail - ioState.ring.sq.sqe_tail <= 4);

                assert(md.serverFD != -1);
                bool mdIssued = md.cursor > 0;
                assert(oe.curLabResult.seenOrders.empty() || mdIssued);

                break;
            }
        }
        return true;
    }

    void oeHandshakeState() const {
        // use SO_ACCEPTCONN to check whether the server is listening

        int isListen;
        socklen_t optlen = sizeof(isListen);
        int ret = getsockopt(oe.serverFD, SOL_SOCKET, SO_ACCEPTCONN, &isListen, &optlen);
        if (ret == -1) {
            cerr << "Failed getsockopt [" << errno << "]." << endl;
            close(oe.serverFD);
            exit(EXIT_FAILURE);
        }

        assert(isListen == 1);
        assert(oe.clientFD != -1);
        assert(oe.connectionSeen > 0);
        assert(oe.curLabResult.connectionTime > 0);
        assert(oe.curLabResult.seenOrders.empty());

    }
};

int main() {
    const string &ofname = "/home/jc/CLionProjects/hft/data/trades_500M.csv";
    const string &fname = "/home/jc/CLionProjects/hft/data/deribit_incremental_book_L2_2020-04-01_BTC-PERPETUAL.csv.gz";
    std::ifstream ifile(fname, std::ios_base::in | std::ios_base::binary);
    if (!ifile) {
        std::cerr << "Failed to open the file." << std::endl;
        exit(EXIT_FAILURE);
    }
    const string headerLine = "exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount";

    Exchange exchange{ifile, headerLine, ofname};
    exchange.run();
}
