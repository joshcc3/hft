//
// Created by jc on 04/11/23.
//

#include "defs.h"

#include <unordered_map>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <utility>
#include <vector>
#include <netinet/in.h>
#include <cassert>
#include <cstdlib>
#include <liburing.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <bitset>


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


struct LabResult {
    TimeNs connectionTime;
    TimeNs disconnectTime;
    std::vector<OrderInfo> seenOrders;

    LabResult() : connectionTime{0}, disconnectTime{0}, seenOrders{} {}

    [[nodiscard]] bool empty() const {
        assert(seenOrders.empty() == (connectionTime == 0));
        return seenOrders.empty();
    }
};


class MDServer {
public:
    static constexpr int MD_SEND_TAG = 3;
    static constexpr int SND_BUF_SZ = 1 << 16;
    static constexpr int SND_BUF_HIGH_WATERMARK = int(SND_BUF_SZ * 0.7);

    const string headerLine;

    std::unique_ptr<u8[]> buffer;
    u64 sentBytes = 0;
    u64 ackedBytes = 0;


    IOUringState &ioState;
    int serverFD = -1;
    struct sockaddr_in multicast_sockaddr{};
    MDMsgId cursor = 0;

    std::ifstream &ifile;
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf;
    std::istream instream;

    std::unordered_map<MDMsgId, TimeNs> eventTimeMp;

    MDServer(IOUringState &ioState, std::ifstream &ifile, string headerLine) :
            headerLine{std::move(headerLine)},
            ioState{ioState},
            ifile{ifile},
            inbuf{},
            instream{&inbuf},
            buffer{std::make_unique<u8[]>(SND_BUF_SZ)} {

        // Create a socket for sending to a multicast address
        serverFD = socket(AF_INET, SOCK_DGRAM, 0);
        if (serverFD < 0) {
            cerr << "Mcast socket [" << errno << "]." << endl;
            exit(EXIT_FAILURE);
        }

        assert(serverFD <= 5); // assume this is the first socket opened


        if (setsockopt(serverFD, SOL_SOCKET, SO_SNDBUF, &SND_BUF_SZ, sizeof(SND_BUF_SZ)) < 0) {
            perror("setsockopt SO_SNDBUF");
            close(serverFD);
            exit(EXIT_FAILURE);
        }
        int bufSize;
        socklen_t bufLen = sizeof(bufSize);
        if (getsockopt(serverFD, SOL_SOCKET, SO_SNDBUF, static_cast<void *>(&bufSize), &bufLen) < 0) {
            close(serverFD);
            perror("Get socket size failed");
            exit(EXIT_FAILURE);
        }
        assert(bufSize == 2 * SND_BUF_SZ);

        unsigned char loopback = 1;
        if (setsockopt(serverFD, IPPROTO_IP, IP_MULTICAST_LOOP,
                       &loopback, sizeof(loopback)) < 0) {
            close(serverFD);
            perror("setsockopt() failed");
            exit(EXIT_FAILURE);
        }


        // Set up the sockaddr structure
        multicast_sockaddr.sin_family = AF_INET;
        multicast_sockaddr.sin_addr.s_addr = inet_addr(MCAST_ADDR.c_str());
        multicast_sockaddr.sin_port = htons(MCAST_PORT);

        if (bind(serverFD, (struct sockaddr *) &multicast_sockaddr, sizeof multicast_sockaddr) < 0) {
            close(serverFD);
            cerr << "Bind failed [" << errno << "]." << endl;
            exit(EXIT_FAILURE);
        }

        reset();

    }

    void fillMDPacket(u8 *&bufPos, u32 &bufLen, TimeNs timeNow, const string &line) {

        TimeNs timestamp = 0;
        TimeNs localTimestamp = 0;
        Side side;
        PriceL price = -1;
        Qty qty = -1;
        bool isBid;
        bool isSnapshot;

        parseDeribitMDLine(line.c_str(), timestamp, localTimestamp, isSnapshot, side, price, qty);
        assert(timestamp && localTimestamp);
        assert(side == Side::BUY || side == Side::SELL);
        assert(price && qty);

        isBid = side == Side::BUY;

        MDPacket &packet = *reinterpret_cast<MDPacket *>(bufPos);
        packet.seqNo = cursor;
        packet.localTimestamp = timeNow;
        packet.price = price;
        packet.qty = qty;
        packet.flags.isBid = isBid;
        packet.flags.isSnapshot = isSnapshot;
        packet.flags.isSnapshot = false;

        assert(packet.seqNo == cursor);
        assert(packet.localTimestamp == timeNow);
        assert(packet.price == price);
        assert(packet.qty == qty);
        assert(packet.flags.isBid == isBid);
        assert(packet.flags.isSnapshot == isSnapshot);
        assert(!packet.flags.isTerm);

        bufPos += sizeof(packet);
        bufLen += sizeof(packet);

        eventTimeMp.emplace(cursor++, timeNow);

    }

    void prepBuffer() {
        assert(serverFD != -1);
        assert(!instream.eof());

        assert(io_uring_sq_space_left(&ioState.ring) > 1);
        assert(io_uring_cq_ready(&ioState.ring) < ioState.ring.cq.ring_entries - 2);
        if (sentBytes - ackedBytes < SND_BUF_HIGH_WATERMARK) {

            auto timeNow = currentTimeNs();

            u32 bufLen = 0;
            u8 *bufStart = buffer.get();
            u8 *bufPos = bufStart;
            string line;
            for (int i = 0; i < 5 && !instream.eof(); ++i) {
                std::getline(instream, line);
                if (!line.empty()) {
                    fillMDPacket(bufPos, bufLen, timeNow, line);
                } else {
                    MDPacket &terminationPacket = *reinterpret_cast<MDPacket *>(bufPos);
                    terminationPacket.flags.isTerm = true;
                    bufPos += sizeof(MDPacket);
                    bufLen += sizeof(MDPacket);
                    assert(instream.eof());
                }
            }

            assert(bufLen < SND_BUF_SZ && bufLen > 0);
            io_uring_sqe *mdSqe = ioState.getSqe(MD_SEND_TAG + sentBytes);

            sentBytes += bufLen;

            assert(multicast_sockaddr.sin_addr.s_addr > 0);
            assert(multicast_sockaddr.sin_port > 0);
            int flags = MSG_CONFIRM | MSG_DONTWAIT;
            io_uring_prep_sendto(mdSqe, serverFD, bufStart, bufLen, flags,
                                 (const struct sockaddr *) &multicast_sockaddr,
                                 sizeof(multicast_sockaddr));
        }

    }

    void reset() {
        cursor = 0;

        if (!inbuf.empty()) {
            assert(inbuf.size() == 2);
            inbuf.pop();
            inbuf.pop();
        }
        ifile.clear();
        ifile.seekg(0);
        assert(ifile.tellg() == 0);
        inbuf.push(boost::iostreams::gzip_decompressor());
        inbuf.push(ifile);
        instream.clear();
        instream.rdbuf(&inbuf);

        string header;
        std::getline(instream, header);
        assert(header == headerLine);

        sentBytes = 0;
        ackedBytes = 0;

        eventTimeMp.clear();
    }

    bool isAlive() const {
        return !instream.eof();
    }

    TimeNs eventTime(MDMsgId id) const {
        assert(id < cursor);
        assert(eventTimeMp.find(id) != eventTimeMp.end());
        return eventTimeMp.find(id)->second;
    }

    bool validEvent(MDMsgId id) const {
        return eventTimeMp.find(id) != eventTimeMp.end();
    }

    void completeMessage(u64 userData, int bytesWrtten) {
        assert(userData >= MD_SEND_TAG);
        u64 ackByte = userData - MD_SEND_TAG + bytesWrtten;
        assert(bytesWrtten > 0);
        assert(ackByte <= sentBytes);
        assert(bytesWrtten <= ackByte);
        assert(bytesWrtten < SND_BUF_SZ - SND_BUF_HIGH_WATERMARK);
        assert(ackByte != ackedBytes);
        if (ackByte > ackedBytes) {
            ackedBytes = ackByte;
        }
    }

    void throttle(u64 userData) const {
        assert(userData > MD_SEND_TAG);
        u64 throttledBytes = userData - MD_SEND_TAG;
        assert(throttledBytes > ackedBytes);
        assert(throttledBytes < sentBytes);
        assert(false);

    }
};


class OEServer {
public:
    const int ACCEPT_TAG = 0;
    const int BUFFER_TAG = 1;
    const int RECV_TAG = 2;

    IOUringState &ioState;
    const MDServer &md;

    bool isAlive;
    int serverFD = -1;
    sockaddr_in serverAddr{};

    static constexpr int BUFFER_SIZE = 1 << 9;
    static constexpr int NUM_BUFFERS = 1;
    static constexpr int GROUP_ID = 1;
    std::unique_ptr<char[]> buffers;
    std::bitset<NUM_BUFFERS> used;

    int connectionSeen = 0;
    int clientFD = -1;
    sockaddr_in clientAddr{};
    socklen_t clientAddrSz = sizeof(clientAddr);

    LabResult curLabResult{};


    OEServer(IOUringState &ioState, const MDServer &md) : ioState{ioState}, md{md}, isAlive{false},
                                                          buffers{std::make_unique<char[]>(BUFFER_SIZE * NUM_BUFFERS)},
                                                          used{} {

        serverFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (serverFD == -1) {
            cerr << "Could not create oe server [" << errno << "]" << endl;
            exit(EXIT_FAILURE);
        }
        assert(serverFD == md.serverFD + 1); // assume that this is the second socket opened

        int enable = 1;
        if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
            throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
        }
        enable = 1;
        if (setsockopt(serverFD, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) < 0) {
            throw std::runtime_error("setsockopt(SO_KEEPALIVE) failed");
        }
        int initialProbe = 10;
        if (setsockopt(serverFD, IPPROTO_TCP, TCP_KEEPIDLE, &initialProbe, sizeof(initialProbe)) < 0) {
            throw std::runtime_error("setsockopt(TCP_KEEPIDLE) failed");
        }
        int subsequentProbes = 2;
        if (setsockopt(serverFD, IPPROTO_TCP, TCP_KEEPINTVL, &subsequentProbes, sizeof(subsequentProbes)) < 0) {
            throw std::runtime_error("setsockopt(TCP_KEEPINTVL) failed");
        }
        int maxProbes = 2;
        if (setsockopt(serverFD, IPPROTO_TCP, TCP_KEEPCNT, &maxProbes, sizeof(maxProbes)) < 0) {
            throw std::runtime_error("setsockopt(TCP_KEEPCNT) failed");
        }


        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(OE_PORT);

        if (bind(serverFD, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        if (listen(serverFD, 1) < 0) {
            throw std::runtime_error("listen(serverDF, 1) failed");
        }

        reset();

    }

    ~OEServer() {
        reset();
        if (serverFD != -1) {
            close(serverFD);
            serverFD = -1;
        }
    }

    void prepAccept() {
        assert(clientFD == -1);
        assert(!isAlive);
        assert(clientAddrSz > 0);
        assert(clientAddr.sin_addr.s_addr == 0);
        assert(curLabResult.seenOrders.empty());
        assert(curLabResult.connectionTime == 0);
        assert(curLabResult.disconnectTime == 0);

        io_uring_sqe *acceptSqe = ioState.getSqe(ACCEPT_TAG);
        clientAddrSz = sizeof(clientAddr);
        assert(fcntl(serverFD, F_GETFD) != -1);
        io_uring_prep_accept(acceptSqe, serverFD, (struct sockaddr *) &clientAddr, &clientAddrSz,
                             SOCK_NONBLOCK | O_CLOEXEC);
    }

    void receiveConn(io_uring_cqe *completion) {
        isAlive = true;
        ++connectionSeen;
        clientFD = completion->res;

        curLabResult.connectionTime = currentTimeNs();
        if (clientFD == -1) {
            cerr << "Failed to accept [" << -clientFD << "]." << endl;
            exit(EXIT_FAILURE);
        }
        assert((io_uring_cqe_get_data64(completion)) == ACCEPT_TAG);
    }

    [[nodiscard]] bool connectionAlive() const {
        return isAlive;
    }

    void reset() {
        assert(!isAlive);
        curLabResult.disconnectTime = currentTimeNs();

        for (const auto &orderInfo: curLabResult.seenOrders) {

            cout << connectionSeen << "," << curLabResult.connectionTime << "," << curLabResult.disconnectTime << "," <<
                 orderInfo.orderInfo.submittedTime << "," << orderInfo.receivedTime << "," <<
                 orderInfo.orderInfo.triggerReceivedTime << "," << orderInfo.triggerSubmitTime << "," <<
                 orderInfo.orderInfo.triggerEvent << "," << orderInfo.orderInfo.flags.isBid << "," << orderInfo.orderInfo.price << "," <<
                 orderInfo.orderInfo.qty << "," << orderInfo.orderInfo.id << "\n";
        }
        cout << endl;
        if (clientFD != -1) {
            close(clientFD);
            clientFD = -1;
        }
        isAlive = false;
        clientFD = -1;
        clientAddrSz = sizeof(clientAddr);
        curLabResult.seenOrders.clear();
        curLabResult.connectionTime = 0;
        curLabResult.disconnectTime = 0;

    }

    void prepareRecv() {
        assert(used.all() || !used.any());
        used.reset();

        io_uring_sqe *bufferSqe = ioState.getSqe(BUFFER_TAG);
        io_uring_prep_provide_buffers(bufferSqe, buffers.get(), BUFFER_SIZE, NUM_BUFFERS, GROUP_ID, 0);
        assert(io_uring_sq_ready(&ioState.ring) == 1);
        int completed = io_uring_submit_and_wait(&ioState.ring, 1);
        assert(completed == 1);
        assert(io_uring_sq_ready(&ioState.ring) == 0);
        {
            cqe_guard g{ioState};
            assert(g.completion->res == NUM_BUFFERS || g.completion->res == 0);
            assert((io_uring_cqe_get_data64(g.completion)) == BUFFER_TAG);

        }

        assert(clientFD != -1);
        assert(isAlive);
        io_uring_sqe *recvSqe = ioState.getSqe(RECV_TAG);
        io_uring_prep_recv_multishot(recvSqe, clientFD, nullptr, 0, 0);
        assert(recvSqe->flags == 0);
        int ogFlags = recvSqe->flags;
        recvSqe->flags |= IOSQE_BUFFER_SELECT;
        assert(recvSqe->flags == (ogFlags | IOSQE_BUFFER_SELECT));
        recvSqe->buf_group = GROUP_ID;
    }

    void completeMessages(io_uring_cqe &completion) {
        assert(isAlive);
        assert(connectionSeen > 0);
        assert(curLabResult.connectionTime > 0);


        auto curTime = currentTimeNs();

        size_t ogLabResSize = curLabResult.seenOrders.size();

        u64 userData = io_uring_cqe_get_data64(&completion);

        bool multishotDone = false;
        assert(userData == RECV_TAG);
        int returnCode = completion.res;
        assert(returnCode > 0 || returnCode != EBADF);
        assert(completion.flags & IORING_CQE_F_BUFFER);
        assert(!multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0));
        multishotDone = multishotDone || ((completion.flags & IORING_CQE_F_MORE) == 0);
        assert(isAlive || returnCode > 0 || returnCode == EBADF);
        isAlive = isAlive && !(returnCode > 0 || returnCode == EBADF);

        if (isAlive) {
            u32 bufferIx = completion.flags >> (sizeof(completion.flags) * 8 - 16);
            assert(bufferIx < NUM_BUFFERS);
            assert(!used.test(bufferIx));

            char *buf = buffers.get() + bufferIx * BUFFER_SIZE;
            int bytesRead = returnCode;
            u32 numMessages = bytesRead / sizeof(Order);
            assert(bytesRead % sizeof(Order) == 0);

            char *endBuf = handleMessages(buf, numMessages, curLabResult, curTime);
            assert(endBuf == buf + sizeof(Order) * numMessages);

            used.set(bufferIx);
        }

        assert(curLabResult.seenOrders.size() > ogLabResSize);
        assert(io_uring_cq_ready(&ioState.ring) == 0);
        assert(io_uring_sq_ready(&ioState.ring) == 0);

        if (multishotDone && isAlive) {
            assert(used.all());
            prepareRecv();
        }

    }

private:
    char *handleMessages(char *buf, u32 n, LabResult &lab, TimeNs curTime) {
        for (int i = 0; i < n; ++i) {

            const auto *o = reinterpret_cast<Order *>(buf);
            auto timeNow = currentTimeNs();
            OrderId orderId = o->id;
            TimeNs triggerSubmitTime = md.eventTime(o->triggerEvent);

            assert(std::abs(o->submittedTime - timeNow) < 1e6);
            assert(md.validEvent(o->triggerEvent));
            assert(std::abs(o->triggerReceivedTime - triggerSubmitTime));
            assert(o->id > 0 && find_if(curLabResult.seenOrders.begin(), curLabResult.seenOrders.end(),
                                        [orderId](const OrderInfo &order) { return order.orderInfo.id == orderId; }) ==
                                curLabResult.seenOrders.end());
            assert(o->price > 0);
            assert(o->qty > 0);

            lab.seenOrders.emplace_back(*o, curTime, triggerSubmitTime);
            buf += sizeof(Order);
        }

        return buf;
    }

};


class Exchange {
    ExchangeState state;
    IOUringState ioState;
    MDServer md;
    OEServer oe;


public:
    Exchange(std::ifstream &f, const string &headerLine) : state(ExchangeState::INIT), ioState(), oe{ioState, md},
                                                           md{ioState, f, headerLine} {
        assert(io_uring_sq_ready(&ioState.ring) == 0);
    }

    [[noreturn]] void run() {
        while (true) {
            switch (state) {
                case ExchangeState::INIT: {
                    oe.prepAccept();
                    assert(io_uring_sq_ready(&ioState.ring) == 1);
                    int completedEvents = io_uring_submit_and_wait(&ioState.ring, 1);
                    assert(completedEvents == 1);
                    assert(io_uring_cq_ready(&ioState.ring) == 1);
                    {
                        cqe_guard cg{ioState};
                        oe.receiveConn(cg.completion);
                    }
                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(io_uring_cq_ready(&ioState.ring) == 0);
                    state = ExchangeState::HANDSHAKE;
                    break;
                }
                case ExchangeState::HANDSHAKE: {
                    oe.prepareRecv();
                    assert(io_uring_sq_ready(&ioState.ring) == 1);
                    md.prepBuffer();
                    assert(io_uring_sq_ready(&ioState.ring) == 2);
                    int submittedEvents = ioState.submit();
                    assert(submittedEvents == 2);

                    assert(io_uring_sq_ready(&ioState.ring) == 0);
                    assert(io_uring_cq_ready(&ioState.ring) >= 0);

                    state = ExchangeState::TRANSMIT;

                    break;
                }
                case ExchangeState::TRANSMIT: {
                    // check for received messages from order entry and enqueue more reads
                    // write out marketdata

                    if (!oe.connectionAlive() || !md.isAlive()) {
                        oe.reset();
                        md.reset();
                        io_uring_submit(&ioState.ring);
                        assert(io_uring_sq_ready(&ioState.ring) == 0);
                        assert(io_uring_cq_ready(&ioState.ring) == 0);
                        state = ExchangeState::INIT;
                    } else {
                        assert(!io_uring_cq_has_overflow(&ioState.ring));
                        assert(io_uring_sq_ready(&ioState.ring) == 0);

                        if (unsigned int ready = io_uring_cq_ready(&ioState.ring)) {
                            io_uring_cqe *es;
                            int seen = io_uring_wait_cqe_nr(&ioState.ring, &es, ready);
                            assert(seen == 0);

                            for (int i = 0; i < ready; ++i) {
                                io_uring_cqe &e = es[i];
                                u64 userData = (io_uring_cqe_get_data64(&e));
                                if (userData == oe.RECV_TAG) {
                                    assert(oe.isAlive);
                                    oe.completeMessages(e);
                                } else if (userData >= MDServer::MD_SEND_TAG &&
                                           userData <= MDServer::MD_SEND_TAG + md.sentBytes) {
                                    int resultCode = e.res;
                                    if (resultCode > 0) {
                                        md.completeMessage(userData, resultCode);
                                    } else if (resultCode == EAGAIN || resultCode == EALREADY) {
                                        cout << "OS Throttle [" << strerror(-resultCode) << "]. " << endl;
                                        md.throttle(userData);
                                    } else {

                                    }
                                } else {
                                    assert(false);
                                }

                            }
                            io_uring_cq_advance(&ioState.ring, ready);
                        }

                        assert(io_uring_cq_ready(&ioState.ring) == 0);
                        unsigned int pending = io_uring_sq_ready(&ioState.ring);
                        assert(pending <= 1);

                        md.prepBuffer();
                        if (io_uring_sq_ready(&ioState.ring) > 0) {
                            io_uring_submit(&ioState.ring);
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
        assert(md.sentBytes < (1 << 10) || md.sentBytes * 0.7 <= md.ackedBytes ||
               md.sentBytes - md.ackedBytes < (MDServer::SND_BUF_SZ / 8));
        assert(md.cursor * 50 <= md.sentBytes);

        switch (state) {
            case ExchangeState::INIT: {
                assert(io_uring_cq_ready(&ioState.ring) == 0);

                assert(ioState.ring.sq.sqe_tail - ioState.ring.sq.sqe_head == 1);

                assert(oe.clientFD == -1);
                assert(oe.connectionSeen == 0 || oe.serverFD != -1);
                assert(oe.curLabResult.empty());

                assert(oe.connectionSeen == 0 || md.serverFD != -1);
                assert(md.cursor == 0);
                md.instream.get();
                assert(md.instream.fail());

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

                oeHandshakeState();

                assert(md.serverFD != -1);
                bool mdIssued = md.cursor > 0;
                assert(oe.curLabResult.seenOrders.empty() || mdIssued);
                assert(!md.instream.eof());

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
        assert(oe.curLabResult.disconnectTime == 0);

    }
};

int main() {
    const string &fname = "/home/jc/CLionProjects/hft/data/deribit_incremental_book_L2_2020-04-01_BTC-PERPETUAL.csv.gz";
    std::ifstream ifile(fname, std::ios_base::in | std::ios_base::binary);
    if (!ifile) {
        std::cerr << "Failed to open the file." << std::endl;
        exit(EXIT_FAILURE);
    }
    const string headerLine = "exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount";

    Exchange exchange{ifile, headerLine};
    exchange.run();
    return 0;
}
