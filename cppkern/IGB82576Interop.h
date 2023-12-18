//
// Created by joshuacoutinho on 20/12/23.
//

#ifndef IGB82576INTEROP_H
#define IGB82576INTEROP_H

#include "defs.h"
#define JOSH_RX_PAGE_IN_CACHE_SHIFT 0
#define JOSH_RX_PAGE_PROCESSED_SHIFT 1

#define MD_PACKET_TYPE 1
#define OE_PACKET_TYPE 2

// #define NDEBUG


#ifdef __cplusplus
extern "C" {

void bug(void);

void pr_info__(const char *str, ...);

[[noreturn]] void __attribute__((cold)) panic__(const char *fmt, ...);

void ssleep__(unsigned int seconds);

#define BUG() panic__("Panic!");

void printf__(const char *fmt, ...);

#else
void bug(void);
#endif

void *malloc(long unsigned int sz);

void free(void *);

u8 syncPacket(void *rx, u16 *pktSz, u8 *isFrag, int frameSz, int numDesc);

u8 *getOurPacket(void *_qvec);

void getPointers(void *rx, int *ogNTC, int *ogNTU, int *ogNTA, int *count);

void incNTC(void *rx);

void setNTC(void *rx, int ntc);

void lockWriteBuf(void *adapter, void *nq, int cpu);

u8 isStartCmdPacket(void *rx);

void trigger_write(void *txQ_, void *nq_, int txIdx, void *data, int dataSz);

void state_check(void *adapter, int cpu);

void checkAdapter(void *adapter);

void *getDevice(const char *devName);

void *getAdapter(void *device);

int getCpu(void);

void *getTxRing(void *adapter, int cpu, int numTxDesc);

void *getNetdevQ(void *txRing);

void q_get_ptrs(void *q, int *nta, int *ntc, int *ntu);

u64 currentTimeNs(void);

int ring_get_ntu(void *ring);

void __cxa_pure_virtual(void);

void __cxa_guard_release(void);

bool __cxa_guard_acquire(void);



#ifndef __cplusplus
	void __assert_fail(void);
	i32 __popcountdi2(i64 a);
#endif

#ifdef __cplusplus
}
#endif

#endif //IGB82576INTEROP_H
