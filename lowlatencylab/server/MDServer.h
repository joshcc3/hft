//
// Created by jc on 09/11/23.
//

#ifndef LLL_EXCHANGE_MDSERVER_H
#define LLL_EXCHANGE_MDSERVER_H

#include <bitset>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <liburing.h>
#include <cstdlib>
#include <cassert>
#include <netinet/in.h>
#include <vector>
#include <utility>
#include <mutex>
#include <thread>
#include <string>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <memory>
#include "OEServer.h"
#include "../defs.h"

class MDServer {
public:

    using gzip_streambuf = boost::iostreams::filtering_streambuf<boost::iostreams::input>;

    static constexpr u64 MD_SEND_TAG = 4;
    static constexpr int SND_BUF_SZ = 1 << 8;
    static constexpr int SND_BUF_HIGH_WATERMARK = int(SND_BUF_SZ * 0.7);
    static constexpr int PACKET_SIZE = sizeof(MDPacket) * 5;
    static constexpr TimeNs DISCONNECT_TIMEOUT = 2'000'000'000;

    const std::string headerLine;

    bool alive = false;
    TimeNs lastReceivedNs = 0;

    std::unique_ptr<u8[]> buffer;
    u64 sentBytes = 0;
    u64 ackedBytes = 0;


    IOUringState &ioState;
    int serverFD = -1;
    struct sockaddr_in unicast_addr{};
    MDMsgId cursor = 0;

    std::ifstream &ifile;
    std::unique_ptr<gzip_streambuf> inbuf = nullptr;
    std::unique_ptr<std::istream> instream = nullptr;

    std::unordered_map<MDMsgId, TimeNs> eventTimeMp;

    MDServer(IOUringState &ioState, std::ifstream &ifile, std::string headerLine) :
            headerLine{std::move(headerLine)},
            ioState{ioState},
            ifile{ifile},
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
//        assert(bufSize == 2 * SND_BUF_SZ);


        // Set up the sockaddr structure
        unicast_addr.sin_family = AF_INET;
        unicast_addr.sin_addr.s_addr = inet_addr(MD_UNICAST_ADDR.c_str());
        unicast_addr.sin_port = htons(MD_UNICAST_PORT);

        reset();

    }

    void fillMDPacket(u8 *&bufPos, u32 &bufLen, TimeNs timeNow, const std::string &line) {

        TimeNs timestamp = 0;
        TimeNs localTimestamp = 0;
        Side side;
        PriceL price = -1;
        Qty qty = -1;
        bool isBid;
        bool isSnapshot;

        parseDeribitMDLine(line.c_str(), timestamp, localTimestamp, isSnapshot, side, price, qty);
        assert(timestamp && localTimestamp);
        assert(side == BUY || side == SELL);
        assert(price);

        isBid = side == BUY;

        MDPacket &packet = *reinterpret_cast<MDPacket *>(bufPos);
        packet.seqNo = cursor;
        packet.localTimestamp = timeNow;
        packet.price = price;
        packet.qty = qty;
        packet.flags.isBid = isBid;
        packet.flags.isSnapshot = isSnapshot;
        packet.flags.isTerm = false;

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

    void send() {
        assert(serverFD != -1);
        assert(!instream->eof());

        if (sentBytes - ackedBytes < SND_BUF_HIGH_WATERMARK) {

            auto timeNow = currentTimeNs();

            u32 bufLen = 0;
            u8 *bufStart = buffer.get();
            u8 *bufPos = bufStart;
            std::string line;
            for (int i = 0; i < PACKET_SIZE / sizeof(MDPacket) && !instream->eof(); ++i) {
                std::getline(*instream, line);
                if (!line.empty()) {
                    fillMDPacket(bufPos, bufLen, timeNow, line);
                } else {
                    MDPacket &terminationPacket = *reinterpret_cast<MDPacket *>(bufPos);
                    terminationPacket.flags.isTerm = true;
                    bufPos += sizeof(MDPacket);
                    bufLen += sizeof(MDPacket);
                    assert(instream->eof());
                }
            }

            assert(bufLen < SND_BUF_SZ && bufLen > 0);
            sentBytes += bufLen;

            assert(unicast_addr.sin_addr.s_addr > 0);
            assert(unicast_addr.sin_port > 0);
            int flags = MSG_CONFIRM;
            sendto(serverFD, bufStart, bufLen, flags,
                                 (const struct sockaddr *) &unicast_addr,
                                 sizeof(unicast_addr));
            ackedBytes = sentBytes;
        }

    }

    bool prepBuffer() {
        assert(serverFD != -1);
        assert(!instream->eof());

        assert(io_uring_sq_space_left(&ioState.ring) >= 1);
        assert(io_uring_cq_ready(&ioState.ring) < ioState.ring.cq.ring_entries);
        if (sentBytes - ackedBytes < SND_BUF_HIGH_WATERMARK) {

            auto timeNow = currentTimeNs();

            u32 bufLen = 0;
            u8 *bufStart = buffer.get();
            u8 *bufPos = bufStart;
            std::string line;
            for (int i = 0; i < PACKET_SIZE / sizeof(MDPacket) && !instream->eof(); ++i) {
                std::getline(*instream, line);
                if (!line.empty()) {
                    fillMDPacket(bufPos, bufLen, timeNow, line);
                } else {
                    MDPacket &terminationPacket = *reinterpret_cast<MDPacket *>(bufPos);
                    terminationPacket.flags.isTerm = true;
                    bufPos += sizeof(MDPacket);
                    bufLen += sizeof(MDPacket);
                    assert(instream->eof());
                }
            }

            assert(bufLen < SND_BUF_SZ && bufLen > 0);
            io_uring_sqe *mdSqe = ioState.getSqe(MD_SEND_TAG + sentBytes);
            sentBytes += bufLen;

            assert(unicast_addr.sin_addr.s_addr > 0);
            assert(unicast_addr.sin_port > 0);
            int flags = MSG_CONFIRM;
            io_uring_prep_sendto(mdSqe, serverFD, bufStart, bufLen, flags,
                                 (const struct sockaddr *) &unicast_addr,
                                 sizeof(unicast_addr));

            return true;
        } else {
            return false;
        }

    }

    void reset() {

        inbuf = std::make_unique<gzip_streambuf>();
        instream = std::make_unique<std::istream>(inbuf.get());

        assert(inbuf.get() != nullptr);
        assert(instream.get() != nullptr);

        cursor = 0;

        ifile.clear();
        ifile.seekg(0);
        assert(ifile.tellg() == 0);

        inbuf->push(boost::iostreams::gzip_decompressor());
        inbuf->push(ifile);

        std::string header;
        std::getline(*instream, header);
        auto pos = header.rfind(headerLine);
        assert(pos == header.size() - headerLine.size());

        alive = true;
        lastReceivedNs = 0;

        sentBytes = 0;
        ackedBytes = 0;

        eventTimeMp.clear();
    }

    bool isAlive() {
        auto now = currentTimeNs();
        alive = alive && (ackedBytes < sentBytes || now - lastReceivedNs < DISCONNECT_TIMEOUT);
        return alive;
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
//        assert(bytesWrtten < SND_BUF_SZ - SND_BUF_HIGH_WATERMARK);
        assert(ackByte != ackedBytes);
        if (ackByte > ackedBytes) {
            ackedBytes = ackByte;
        }

        TimeNs now = currentTimeNs();
        assert(!alive || lastReceivedNs == 0 || now - lastReceivedNs < DISCONNECT_TIMEOUT);
        lastReceivedNs = now;

    }

    void throttle(u64 userData) const {
        assert(userData > MD_SEND_TAG);
        u64 throttledBytes = userData - MD_SEND_TAG;
        assert(throttledBytes > ackedBytes);
        assert(throttledBytes < sentBytes);
        assert(false);

    }

    bool isEOF() const {
        return instream->eof();
    }
};

#endif //LLL_EXCHANGE_MDSERVER_H
