//
// Created by jc on 03/11/23.
//

#ifndef TICTACTOE_L2OB_H
#define TICTACTOE_L2OB_H

#include "defs.h"
#include "../../cppkern/containers.h"

using namespace josh;

struct TopLevel {
    PriceL bestBid = 0;
    PriceL bestAsk = 0;
    Qty bidSize = -1;
    Qty askSize = -1;

    TopLevel() = default;

    void set(PriceL _bestBid, PriceL _bestAsk, Qty _bidSize, Qty _askSize) {
        bestBid = _bestBid;
        bestAsk = _bestAsk;
        bidSize = _bidSize;
        askSize = _askSize;
        assert(bidSize > 0);
        assert(askSize > 0);
        assert(bestBid > 0);
        assert(bestAsk > bestBid);
        assert((bestAsk - bestBid) / double(bestBid) < 0.1);
    }
};

struct alignas(32) LevelInfo {
    TimeNs lastUpdated;
    PriceL price;
    Qty levelQty;

    LevelInfo(TimeNs time, PriceL price, Qty qty) : lastUpdated{time}, price{price}, levelQty{qty} {
        assert(time >= 0);
        assert(price >= PRECISION_L);
        assert(qty >= 1);
    }

    void update(TimeNs t, Qty newQty) {
        // assert(newQty != levelQty);
        assert(t >= lastUpdated);
        lastUpdated = t;
        levelQty = newQty;
    }
};

constexpr static int MAX_SZ = 1 << 9;
using SideLevel = array<LevelInfo, MAX_SZ>;
//using SideLevel = vector<LevelInfo>;

class L2OB {
public:
    // debug

    SideLevel bid;
    SideLevel ask;
    i64 spreadEWMA = 0;

    L2OB() : bid{}, ask{} {
    }

    void clear() {
        size_t oldCap = bid.capacity();
        size_t askCap = ask.capacity();
        bid.clear();
        ask.clear();
        assert(bid.capacity() == oldCap);
        assert(ask.capacity() == askCap);
    }

    TopLevel
    update(bool isSnapshot, Side side, TimeNs localTimestamp, PriceL price, Qty qty) {
        assert(qty > 0);
        assert(price >= PRECISION_L);
        assert(localTimestamp > 0);
        TopLevel res;
        if (side == Side::BUY) {
            res = update<Side::BUY>(isSnapshot, bid, ask, localTimestamp, price, qty);
        } else if (side == Side::SELL) {
            res = update<Side::SELL>(isSnapshot, ask, bid, localTimestamp, price, qty);
        } else {
            assert(false);
        }
        return res;
    }

    template<Side side>
    TopLevel
    update(bool isSnapshot, SideLevel& level, SideLevel& oppLevel, TimeNs localTimestamp, PriceL price, Qty qty) {
        assert(oppLevel.empty() || isSnapshot || ((side == Side::BUY) == (price <= oppLevel[0].price)));
        auto sideComp = [price](const LevelInfo& l) {
            if constexpr (side == Side::BUY) {
                return price >= l.price;
            } else {
                return price <= l.price;
            }
        };
        TopLevel topLevel;
        if (!bid.empty() && !ask.empty()) {
            topLevel.set(bid.back().price, ask.back().price, bid.back().levelQty, ask.back().levelQty);
        }

        auto iter = find_if(level.rbegin(), level.rend(), sideComp);
        assert(iter == level.rend() || (sideComp(*iter) && iter->lastUpdated <= localTimestamp));
//        assert(iter != level.rend() || level.empty() ||
  //          (!sideComp(*(iter - 1)) || (((iter - 1)->price == price) && ((iter - 1)->lastUpdated <= localTimestamp)));

        if (iter != level.rbegin()) {
            topLevel.bidSize = -1;
            topLevel.askSize = -1;
        }

        const bool isTopLevelUpdate = iter == level.rbegin();

        if (iter != level.rend() && iter->price == price) {
            // assert(qty != iter->levelQty);
            iter->update(localTimestamp, qty);
        } else {
            // TODO we currently dont treat atomic updates properly which means the book gets into a crossed state sometimes.
            level.insert(iter.base(), localTimestamp, price, qty);
        }

        if (!bid.empty() && !ask.empty() && isTopLevelUpdate) {
            spreadEWMA = spreadEWMA / 2 + (topLevel.bestAsk - topLevel.bestBid) / 2;
        }

        assert(verifyLevel<side>(level, localTimestamp, price, qty));

        assert(stateChecks());
        assert((!bid.empty() && !ask.empty() && isTopLevelUpdate) || (topLevel.bidSize == -1 && topLevel.askSize == -1));
        return topLevel;
    }

    template<Side side>
    bool verifyLevel(SideLevel& level, TimeNs localTimestamp, PriceL price, Qty qty) const {
        auto sideComp = [price](const LevelInfo& l) {
            if constexpr (side == Side::BUY) {
                return price >= l.price;
            } else {
                return price <= l.price;
            }
        };

        auto it = find_if(level.begin(), level.end(), [price](const LevelInfo& l) { return l.price == price; });
        assert(it->price == price && it->levelQty == qty && it->lastUpdated == localTimestamp);
        assert(it == level.begin() || sideComp(*(it - 1)));
        assert(it == level.end() - 1 || !sideComp(*(it + 1)));

        return true;
    }


    void cancel(TimeNs t, PriceL priceL, Side side) {
        // TODO - implement a faster version of find_if which attempts to seek to the correct location.
        // we know that the top layers of the book are likely to be quite dense so you can just jump to the correct location?
        // you could have a sparse representation of the book for the top level where you store every tick level relative to the top
        // and seek to an appropriate level based on the amount of increment.
        // you probably only want to store all  of tick levels as well.

        assert(priceL > PRECISION_L);
        assert(t > 0);
        assert(side == Side::BUY || side == Side::SELL);
        SideLevel& level = side == Side::BUY ? bid : ask;
        auto it = find_if(level.rbegin(), level.rend(),
                          [priceL](const LevelInfo& l) { return l.price == priceL; });
        assert(it != level.rend());
        assert((it.base() - 1)->price == priceL);
        level.erase(it.base() - 1);

        assert(stateChecks());
    }

    bool isCrossed() {
        assert(!bid.empty() && !ask.empty());
        return (bid.end() - 1)->price >= (ask.end() - 1)->price;
    }

    bool stateChecks() {
        if (!bid.empty() && !ask.empty()) {
            auto it = bid.begin();
            while (it + 1 != bid.end()) {
                assert(it->price < (it + 1)->price);
                ++it;
            }
            auto askIt = ask.begin();
            while (askIt + 1 != ask.end()) {
                assert(askIt->price > (askIt + 1)->price);
                ++askIt;
            }
            assert(spreadEWMA >= 0);
            assert((ask.end() - 1)->price - (bid.end() - 1)->price >= -20 * PRECISION_L);
        }

        return true;
    }
};


#endif //TICTACTOE_L2OB_H
