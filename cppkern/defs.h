//
// Created by jc on 03/11/23.
//

#ifndef KERN_DEFS_H
#define KERN_DEFS_H

// #define NDEBUG

#ifdef NDEBUG
#undef assert
#define assert(expr) void(0)
#else
#undef assert
#define assert(expr) if(!(expr)) bug();
#endif



#ifdef __cplusplus
#include <cstdint>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_ether.h>
#include <linux/udp.h>
#else
#include <linux/types.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/udp.h>


#endif

#define OE_PORT 9012
#define CMD_PORT 9013
#define MD_UNICAST_PORT 4321
#define TCP_SENDING_PORT 36262


typedef  uint64_t u64;
typedef  int64_t i64;
typedef  uint32_t u32;
typedef  int32_t i32;
typedef  uint16_t u16;
typedef  int16_t i16;
typedef  uint8_t u8;
typedef  int8_t i8;

struct TCPFrame {
    struct ethhdr eth;
    struct iphdr ip;
    struct tcphdr tcp;
}__attribute__((packed));

struct UDPFrame {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
}__attribute__((packed));


struct MDFrame {
    struct ethhdr eth;
    struct iphdr ip;
    struct udphdr udp;
} __attribute__((packed));


#ifdef __cplusplus

#define ntohl__(x) (u32)__builtin_bswap32((u32)(x))
#define ntohs__(x) (u16)__builtin_bswap16((u16)(x))

#define htonl__(x) (u32)__builtin_bswap32((u32)(x))
#define htons__(x) (u16)__builtin_bswap16((u16)(x))

#endif


#endif //TICTACTOE_DEFS_H
