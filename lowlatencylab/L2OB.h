//
// Created by jc on 03/11/23.
//

#ifndef TICTACTOE_L2OB_H
#define TICTACTOE_L2OB_H

#include <cassert>
#include <unordered_set>
#include "defs.h"

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

struct LevelInfo {

    TimeNs lastUpdated;
    PriceL price;
    Qty levelQty;

    LevelInfo(TimeNs time, PriceL price, Qty qty) : lastUpdated{time}, price{price}, levelQty{qty} {
        assert(time >= 0);
        assert(price >= PRECISION_L);
        assert(qty >= 1);
    }

    void update(TimeNs t, Qty newQty) {
        assert(newQty != levelQty);
        assert(t > lastUpdated);
        lastUpdated = t;
        levelQty = newQty;
    }

};

using SideLevel = std::vector<LevelInfo>;

class L2OB {
public:

    L2OB() : bid{}, ask{}, seen{} {
        bid.reserve(1 << 8);
        ask.reserve(1 << 8);
    }

    void clear() {
        size_t oldCap = bid.capacity();
        size_t askCap = ask.capacity();
        bid.clear();
        ask.clear();
        seen.clear();
        assert(bid.capacity() == oldCap);
        assert(ask.capacity() == askCap);
    }

    TopLevel
    update(Side side, TimeNs localTimestamp, PriceL price, Qty qty) {
        assert(qty > 0);
        assert(price > PRECISION_L);
        assert(localTimestamp > 0);

        if (side == Side::BUY) {
            return update < Side::BUY > (bid, ask, localTimestamp, price, qty);
        } else if (side == Side::SELL) {
            return update < Side::SELL > (ask, bid, localTimestamp, price, qty);
        } else {
            assert(false);
        }
    }

    template<Side side>
    TopLevel
    update(SideLevel &level, SideLevel &oppLevel, TimeNs localTimestamp, PriceL price, Qty qty) {
        assert(oppLevel.empty() || price >= oppLevel[0].price);
        auto sideComp = [price](const LevelInfo &l) {
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

        auto iter = std::find_if(level.rbegin(), level.rend(), sideComp);
        assert(iter == level.rend() || iter->price >= price && iter->lastUpdated < localTimestamp);
        assert(iter != level.rend() || !level.empty() ||
               (iter - 1)->price < price && iter->lastUpdated < localTimestamp);

        if (iter != level.rbegin()) {
            topLevel.bidSize = -1;
            topLevel.askSize = -1;
        }

        if (iter->price == price) {
            assert(seen.find(price) != seen.end());
            assert(qty != iter->levelQty);
            iter->update(localTimestamp, qty);
        } else {
            assert(seen.find(price) == seen.end());
            seen.insert(price);
            level.insert(iter.base(), LevelInfo{localTimestamp, price, qty});
        }

        auto it = std::find_if(level.begin(), level.end(), [price](const LevelInfo &l) { return l.price == price; });
        assert(it->price == price && it->levelQty == qty && it->lastUpdated == localTimestamp);
        assert(it == level.begin() || sideComp(*(it - 1)));
        assert(it == (level.end() - 1) || !sideComp(*(it + 1)));

        assert(stateChecks());

        return topLevel;
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
        SideLevel level = side == Side::BUY ? bid : ask;
        auto it = std::find_if(level.rbegin(), level.rend(),
                               [priceL](const LevelInfo &l) { return l.price == priceL; });
        assert(it != level.rend());
        assert(seen.find(priceL) != seen.end());
        assert((it.base() - 1)->price == priceL);
        level.erase(it.base() - 1);
        seen.erase(priceL);

        assert(seen.find(priceL) == seen.end());
        stateChecks();
    }


    // debug
    std::unordered_set<PriceL> seen;

    SideLevel bid;
    SideLevel ask;

    bool stateChecks() {
        assert(bid.size() + ask.size() == seen.size());
        auto it = bid.begin();
        while ((it + 1) != bid.end()) {
            assert(it->price < (it + 1)->price);
        }
        it = ask.begin();
        while ((it + 1) != ask.end()) {
            assert(it->price > (it + 1)->price);
        }
        assert((bid.end() - 1)->price < (ask.end() - 1)->price);

        std::unordered_set<PriceL> other;
        std::transform(bid.begin(), bid.end(), std::inserter(other, other.begin()),
                       [](const LevelInfo &l) { return l.price; });
        std::transform(ask.begin(), ask.end(), std::inserter(other, other.begin()),
                       [](const LevelInfo &l) { return l.price; });
        assert(other == seen);

    }
};


#endif //TICTACTOE_L2OB_H
