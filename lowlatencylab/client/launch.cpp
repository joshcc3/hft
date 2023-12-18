//
// Created by joshuacoutinho on 20/12/23.
//

#include "launch.h"
#include "IGB82576IO.h"
#include "CoroutineMngr.h"
#include "defs.h"
#include "mdmcclient.h"
#include "../../cppkern/IGB82576Interop.h"

inline Coroutine blockingCo;
inline ContextMgr *ctxM;
inline Driver *driver;
static inline bool initialized = false;

void driverTrampoline(u64 driverAddr)
{

	pr_info__("Initiating driver trampoline");
	auto s = reinterpret_cast<Driver *>(driverAddr);
	assert(s == driver);
	s->run();
	pr_info__("Completed strategy run.");
	s->io.acceptingPkts = false;
	swapcontext_(&ctxM->interruptCtx, &ctxM->blockingRecvCtx);
	assert(false);
	// TODO - put this in a while loop.
}

void initStrategy(void *adapter, void* rx)
{
	pr_info__("Initing strat");
	ctxM = static_cast<ContextMgr *>(malloc(sizeof(ContextMgr)));
	new (ctxM) ContextMgr();
	driver = static_cast<Driver *>(malloc(sizeof(Driver)));
	// TODO - I dont think asserts and pr_info are working
	new (driver) Driver(adapter, rx, *ctxM);
	blockingCo.handle = reinterpret_cast<u64>(driver);
	blockingCo.trampoline = driverTrampoline;
	ctxM->init(blockingCo);
}

void handleFrames(void* rx, int irq) {
	ErrorCode err{};
	//const auto curTime = currentTimeNs();
	pr_info__("Handle Frames");
	int available = 0;

	u8 isFrag = false;

	int ogNTC;
	int ogNTU;
	int ogNTA;
	int count = 0;
	getPointers(rx, &ogNTC, &ogNTU, &ogNTA, &count);
	assert(count);
	// TODO - is it faster to loop prefetching and then loop again going through all the packets?
	while (true) {
		u16 packetSz = 0;
		const u8 pktStatus = syncPacket(rx, &packetSz, &isFrag, IGBConfig::FRAME_SIZE, IGBConfig::NUM_WRITE_DESC);
		if (pktStatus == INVALID_PKT) {
			pr_info__("Bailing on irq %d", irq);
			break;
		}
		if (pktStatus == OK) {

			assert(pktStatus == NICPktStatus::OK);
			assert(packetSz);
			++available;
			assert(available <= count);
			if (u8* pkt = getOurPacket(rx)) {
				const auto handleErr = driver->io.handler(driver->io.blocker, pkt, packetSz);
				err.append(handleErr);
			} else {
			        pr_info__("Ignoring other packet");
			}
		} else {
			assert(pktStatus== ALREADY_PROCESSED || pktStatus == PKT_ERROR);
		}
		incNTC(rx);
	}
	// TODO - handle errors from the packets.
	if(err.isErr()) {
		logErrAndExit(err);
	}

	setNTC(rx, ogNTC);


	int finalNTC;
	int finalNTU;
	int finalNTA;
	int finalCount = 0;

	getPointers(rx, &finalNTC, &finalNTU, &finalNTA, &finalCount);

	assert(ogNTC == finalNTC);
	assert(ogNTU == finalNTU);
	assert(ogNTA == finalNTA);
	assert(count == finalCount);

	if(available > 0) {
		pr_info__("Handle Frames: Initiating swapcontext");
		swapcontext_(&ctxM->interruptCtx, &ctxM->blockingRecvCtx);
		pr_info__("Handle Frames: Return to interrupt context successful");
	}
}


// TODO - deinit the strategy.
void strategyPath(void *rx, void *adapter, const int irq)
{
	if (initialized && driver->io.acceptingPkts) {
		assert(driver->io.acceptingPkts);
		handleFrames(rx, irq);
	} else if(!initialized) {
		u8 isFrag = false;
		u16 pktSz = 0;
		u8 retCode = syncPacket(rx, &pktSz, &isFrag,
					IGBConfig::FRAME_SIZE,
					IGBConfig::NUM_READ_DESC);
		assert(retCode <= NICPktStatus::ALREADY_PROCESSED);
		auto pktStatus = static_cast<NICPktStatus>(retCode);
		assert(retCode == NICPktStatus::INVALID_PKT || pktSz);

		if (pktStatus == INVALID_PKT || pktStatus == PKT_ERROR) {
			return;
		}
		pr_info__("Valid page %d", retCode);
		if (isStartCmdPacket(rx)) {
			pr_info__("Start cmd packet received");
			initialized = true;
			initStrategy(adapter, rx);
			driver->io.acceptingPkts = true;
			pr_info__("Initiating swapcontext");
			swapcontext_(&ctxM->interruptCtx, &ctxM->blockingRecvCtx);
		}
	} else {
		pr_info__("Stable: Driver initialized but not accepting pkts");
	}
}

