//
// Created by jc on 03/11/23.
//

#ifndef TICTACTOE_DEFS_H
#define TICTACTOE_DEFS_H

#include <sys/syscall.h>
#include <fstream>

#include <openssl/sha.h>
#include <iostream>
#include <iomanip>
#include <cstring>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <liburing.h>
#include <chrono>
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <linux/if_ether.h>
#include <linux/udp.h>
#include <netinet/ip.h>
#include <array>
#include <dirent.h>

// #define NDEBUG

#ifdef NDEBUG
#undef assert
#define assert(expr) void(0)
#endif

inline double timeSpent[10] = {0, 0, 0, 0, 0};

template<typename T>
inline double elapsed(T t1, T t2) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() / 1000000000.0;
}

#define CLOCK(c, a) { \
    auto _s = std::chrono::system_clock::now(); \
    a;             \
    auto _e = std::chrono::system_clock::now(); \
    GET_PC(c) += elapsed(_s, _e);                      \
}


using std::cerr;
using std::endl;
using std::cout;


using u64 = uint64_t;
using i64 = int64_t;
using u32 = uint32_t;
using i32 = int32_t;
using u16 = uint16_t;
using i16 = int16_t;
using u8 = uint8_t;
using i8 = int8_t;

using PriceL = i64;
using SeqNo = i64;
using Qty = i32;
using OrderId = u64;
using MDMsgId = SeqNo;

using TimeNs = i64;

constexpr static int PINNED_CPU = 1;
constexpr static int SQ_POLL_PINNED_CPU = 2;

constexpr static int MD_PACKET_TYPE = 1;
constexpr static int OE_PACKET_TYPE = 2;

inline u64 __attribute__((always_inline)) rotl(u64 a, u32 n) {
    return (a << n) | (a >> (64 - n));
}

inline u64 __attribute__((always_inline)) rotr(u64 a, u32 n) {
    return (a >> n) | (a << (64 - n));
}

enum Side {
    BUY,
    SELL
};

static constexpr PriceL PRECISION_L = PriceL(1e6);
static constexpr double PRECISION_D = 1e6;

struct OrderFlags {
    bool isBid: 1;
};

struct Order {
    u8 packetType{OE_PACKET_TYPE};
    u8 _padding[5]{};
    TimeNs submittedTime = 0;
    MDMsgId triggerEvent = 0;
    TimeNs triggerReceivedTime = 0;
    OrderId id = 0;
    PriceL price = 0;
    Qty qty = 0;
    OrderFlags flags{};

    Order() = default;

    Order(TimeNs submittedTime, MDMsgId triggerEvent, TimeNs triggerReceivedTime, OrderId id, PriceL price, Qty qty,
          OrderFlags flags
    ) : submittedTime{submittedTime},
        triggerEvent{triggerEvent},
        triggerReceivedTime{triggerReceivedTime},
        id{id},
        price{price},
        qty{qty},
        flags{flags} {
    }
} __attribute__((packed));

struct OrderFrame {
    ethhdr eth{};
    iphdr ip{};
    udphdr udp{};
    Order o{};
} __attribute__((packed));


struct OrderInfo {
    Order orderInfo;

    TimeNs receivedTime = 0;
    TimeNs triggerSubmitTime = 0;

    OrderInfo(const Order& o, TimeNs r, TimeNs t) : orderInfo{o}, receivedTime{r}, triggerSubmitTime{t} {
    }
};


inline TimeNs currentTimeNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        (std::chrono::system_clock::now()).time_since_epoch()).count();
}

#define GET_PC(c) timeSpent[c]

inline int TOT_RECV_PC = 0;
inline int BOOK_UPDATE_PC = 1;
inline int ORDER_SUBMISSION_PC = 2;
inline int MSG_HANDLING_PC = 3;
inline int SYS_RECV_PC = 4;


inline std::string getProgramNameByTid(pid_t parentTID, pid_t tid) {
    std::string path = "/proc/" + std::to_string(parentTID) + "/task/" + std::to_string(tid) + "/comm";
    std::ifstream commFile(path);
    std::string programName;

    if (commFile.is_open() && std::getline(commFile, programName)) {
        // Successfully read the program name
        return programName;
    } else {
        // Handle the error if the file cannot be opened or read
        return "Error: Unable to read program name";
    }
}

struct IOUringState {
    enum class URING_FD {
        _numFD
    };

    static constexpr ssize_t FILE_TABLE_SZ = int(URING_FD::_numFD);
    static constexpr int QUEUE_DEPTH = 1 << 10;

    std::array<int, FILE_TABLE_SZ> fileTable{};
    io_uring ring{};

    IOUringState(bool enableSQPoll) {
        io_uring_params params;

        if (enableSQPoll) {
            params = {
                .sq_entries = QUEUE_DEPTH,
                .cq_entries = QUEUE_DEPTH * 2,
                .flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF,
                .sq_thread_cpu = SQ_POLL_PINNED_CPU,
                .sq_thread_idle = 30'000,
            };
        } else {
            params = {
                .sq_entries = QUEUE_DEPTH,
                .cq_entries = QUEUE_DEPTH * 2,
                .flags = IORING_SETUP_SINGLE_ISSUER
            };
        }

        if (int error = io_uring_queue_init_params(QUEUE_DEPTH, &ring, &params) < 0) {
            cerr << "Queue init failed [" << error << "]." << endl;
            throw std::runtime_error("Unable to setup io_uring queue");
        }
        assert(params.sq_entries == QUEUE_DEPTH);
        assert(params.cq_entries == QUEUE_DEPTH * 2);
        assert(params.features & IORING_FEAT_SUBMIT_STABLE);
        assert(params.features & IORING_FEAT_CQE_SKIP);

        assert(ring.sq.ring_entries == QUEUE_DEPTH);
        assert(ring.cq.ring_entries == 2 * QUEUE_DEPTH);
        assert(ring.sq.sqe_tail == ring.sq.sqe_head);

        if (FILE_TABLE_SZ > 0) {
            if (const int res = io_uring_register_files_sparse(&ring, FILE_TABLE_SZ); res != 0) {
                cerr << "Register files (Errno " << -res << ")." << endl;
                exit(EXIT_FAILURE);
            }
        }

        if (enableSQPoll) {
            // Wait for process completion to finish
            usleep(100'000);
            // Wait for sq-poll to be scheduled
            pid_t mainTID = syscall(SYS_gettid);
            pid_t guessedSQPollTID = -1;


            std::string path = "/proc/" + std::to_string(mainTID) + "/task/";

            const char* dirPath = path.c_str();
            DIR* dir = opendir(dirPath);

            if (dir == nullptr) {
                std::perror("opendir failed");
                exit(EXIT_FAILURE);
            }

            const dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_name[0] != '.') {
                    int tid = std::stoi(entry->d_name);
                    if (tid != mainTID) {
                        if (guessedSQPollTID != -1) {
                            cerr << "Unexpected number of worker threads" << endl;
                        } else {
                            guessedSQPollTID = tid;
                        }
                    }
                }
            }

            auto comm = getProgramNameByTid(mainTID, guessedSQPollTID);
            if (comm.compare(0, 8, "iou-sqp-") != 0) {
                cerr << "Could not find sq-poll tid" << endl;
                usleep(100'000'000);
                exit(EXIT_FAILURE);
            }

            cout << "Kernel SQ Poll Thread [" << guessedSQPollTID << "] [" << comm << "]." << endl;

            sched_param schparam{};
            const int receiveThreadPolicy = SCHED_FIFO;
            const int priority = 99; // sched_get_priority_max(receiveThreadPolicy);
            schparam.sched_priority = priority;
            if (sched_setscheduler(guessedSQPollTID, receiveThreadPolicy, &schparam)) {
                cerr << "Error [" << errno << " in setting priority: " << strerror(errno) << endl;
                exit(EXIT_FAILURE);
            }
        }
    }

    void registerFD(URING_FD fdName, int fd) {
        const int fileTable[1] = {fd};
        io_uring_register_files_update(&ring, static_cast<int>(fdName), fileTable, 1);
    }

    ~IOUringState() {
        io_uring_queue_exit(&ring);
    }

    [[nodiscard]] io_uring_sqe* getSqe(u64 tag) {
        assert(tag < (u64(1) << 31));
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        assert(nullptr != sqe);
        io_uring_sqe_set_data64(sqe, tag);

        return sqe;
    }

    int submit() {
        int submitted = io_uring_submit(&ring);
        assert(submitted > 0);
        return submitted;
    }

    int submitAndWait(int n) {
        return io_uring_submit_and_wait(&ring, n);
    }

    io_uring_cqe* popCqe() {
        io_uring_cqe* cqe;
        // TODO: I get interrupts here - un sure why
        int res = io_uring_wait_cqe(&ring, &cqe);
        assert(res == 0);
        u64 tag = io_uring_cqe_get_data64(cqe);
        assert(tag >= 0);

        return cqe;
    }
};

struct MDFlags {
    bool isBid: 1;
    bool isSnapshot: 1;
    bool isTerm: 1;
};

struct MDPacket {
    static_assert(sizeof(MDFlags) == 1);
    u8 packetType{MD_PACKET_TYPE};
    char _padding[5];
    SeqNo seqNo = -1;
    TimeNs localTimestamp = 0;
    PriceL price = 0;
    Qty qty = 0;
    MDFlags flags{.isTerm = true};

    MDPacket() = default;

    MDPacket(SeqNo seqNo, TimeNs localTimestamp, PriceL price, Qty qty, MDFlags flags) : seqNo{seqNo},
        localTimestamp{localTimestamp},
        price{price}, qty{qty},
        flags{flags}, _padding{} {
    }

    MDPacket(const MDPacket& other) = default;
};

struct MDFrame {
    ethhdr eth;
    iphdr ip;
    udphdr udp;
} __attribute__((packed));

inline bool operator==(const IOUringState& s1, const IOUringState& s2) {
    return &s1 == &s2;
}

struct cqe_guard {
    io_uring_cqe* completion;
    IOUringState& ring;

    explicit cqe_guard(IOUringState& ring) : ring{ring} {
        completion = ring.popCqe();
    }

    ~cqe_guard() {
        io_uring_cqe_seen(&ring.ring, completion);
    }
};

inline constexpr int OE_PORT = 9012;

inline std::string MCAST_ADDR = "239.255.0.1";
inline constexpr int MCAST_PORT = 12345;

inline std::string MD_UNICAST_HOSTNAME = "lll-1.md.client";
inline constexpr uint16_t MD_UNICAST_PORT = 4321;

inline static constexpr PriceL TRADE_THRESHOLD = PRECISION_L * 500'000'000;


inline int parseDeribitMDLine(const char* msg, TimeNs& timestamp, TimeNs& localTimestamp, bool& isSnapshot, Side& side,
                              PriceL& priceL,
                              Qty& qty) {
    int matched = 6;

    //        string a = "deribit,BTC-PERPETUAL,";
    //        static_assert(sizeof(string) == 24 && a.size() == 22); // small string optimization
    int startIx = 21; // a.size() - 1;

    // 1585699200245000
    int ix = startIx;
    TimeNs _timestamp = 0;
    for (int i = 0; i < 16; ++i) {
        _timestamp = _timestamp * 10 + (msg[++ix] - '0');
    }
    //        assert(_timestamp == timestamp);
    ++ix;
    TimeNs _localtimestamp = 0;
    for (int i = 0; i < 16; ++i) {
        _localtimestamp = _localtimestamp * 10 + (msg[++ix] - '0');
    }
    //        assert(_localtimestamp == localTimestamp);

    ix += 2;
    bool _isSnapshot = msg[ix] == 't';
    //        assert(_isSnapshot == isSnapshot);

    ix += _isSnapshot ? 5 : 6;
    Side _side_ = msg[ix] == 'b' ? Side::BUY : Side::SELL;
    //        assert(side == _side_);

    PriceL _price{};
    ix += 3;
    PriceL decimal = 0;
    while (msg[++ix] != ',' && msg[ix] != '.') {
        decimal = decimal * 10 + (msg[ix] - '0');
    }
    PriceL fraction{};
    if (msg[ix] == '.') {
        PriceL mult = PRECISION_L;
        while (msg[++ix] != ',') {
            fraction = fraction * 10 + (msg[ix] - '0');
            mult /= 10;
        }
        _price = decimal * PRECISION_L + fraction * mult;
    } else {
        _price = decimal * PRECISION_L;
    }
    //        assert(_price == priceL);

    Qty _qty{};
    while (msg[++ix] != '\0') {
        _qty = _qty * 10 + (msg[ix] - '0');
    }
    //        assert(_qty == qty);

    timestamp = _timestamp;
    localTimestamp = _localtimestamp;
    isSnapshot = _isSnapshot;
    side = _side_;
    priceL = _price;
    qty = _qty;
    return matched;
}


inline bool checkMessageDigest(const u8* buf, ssize_t bytes) {
    static std::unordered_map<int, MDPacket> seenHashes{};
    const MDPacket& p = *reinterpret_cast<const MDPacket *>(buf);

    const auto& [e, inserted] = seenHashes.emplace(p.seqNo, p);

    assert(inserted);
    return true;
}


static inline unsigned short from32to16(unsigned int x) {
    /* add up 16-bit and 16-bit for 16+c bit */
    x = (x & 0xffff) + (x >> 16);
    /* add up carry.. */
    x = (x & 0xffff) + (x >> 16);
    return x;
}

static unsigned int do_csum(const unsigned char* buff, int len) {
    unsigned int result = 0;

    assert(len == 20);
    assert(((unsigned long)buff & 0xf) == 0xe);

    if (2 & (unsigned long) buff) {
        result += *(unsigned short *) buff;
        len -= 2;
        buff += 2;
    }
    assert(len >= 4);

    const unsigned char* end = buff + ((unsigned) len & ~3);
    unsigned int carry = 0;

    do {
        unsigned int w = *(unsigned int *) buff;
        buff += 4;
        result += carry;
        result += w;
        carry = (w > result);
    } while (buff < end);

    result += carry;
    result = (result & 0xffff) + (result >> 16);

    if (len & 2) {
        result += *(unsigned short *) buff;
        buff += 2;
    }

    result = from32to16(result);

    return result;
}


inline u16 ip_fast_csum(const u8* iph, u32 ihl) {
    return (u16) ~do_csum(iph, ihl * 4);
}


/*
 * Fold a partial checksum
 * This function code has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static inline __sum16 csum_fold(__wsum csum) {
    u32 sum = (u32) csum;

    sum = (sum & 0xffff) + (sum >> 16);
    sum = (sum & 0xffff) + (sum >> 16);
    return (__sum16) ~sum;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static inline u32 from64to32(u64 x) {
    /* add up 32-bit and 32-bit for 32+c bit */
    x = (x & 0xffffffff) + (x >> 32);
    /* add up carry.. */
    x = (x & 0xffffffff) + (x >> 32);
    return (u32) x;
}

__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
                          __u32 len, __u8 proto, __wsum sum);

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
                          __u32 len, __u8 proto, __wsum sum) {
    unsigned long long s = (u32) sum;

    s += (u32) saddr;
    s += (u32) daddr;
#ifdef __BIG_ENDIAN__
	s += proto + len;
#else
    s += (proto + len) << 8;
#endif
    return (__wsum) from64to32(s);
}

/*
 * This function has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static inline u16
csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
                  __u8 proto, __wsum sum) {
    return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

static inline u16 udp_csum(u32 saddr, u32 daddr, u32 len,
                           u8 proto, u16* udp_pkt) {
    u32 csum = 0;
    u32 cnt = 0;

    /* udp hdr and data */
    for (; cnt < len; cnt += 2)
        csum += udp_pkt[cnt >> 1];

    return csum_tcpudp_magic(saddr, daddr, len, proto, csum);
}


#endif //TICTACTOE_DEFS_H
