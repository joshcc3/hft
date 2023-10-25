//
// Created by jc on 24/10/23.
//
#ifndef HFT_L3ORDERBOOK_H
#define HFT_L3ORDERBOOK_H

#include <cstdint>
#include <cassert>
#include <list>
#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <array>

// TODO - when should you use noexcept

using namespace std;

using u64 = uint64_t;


struct Order {
    bool isStrategy;
    const OrderId orderId;
    const Side side;
    PriceL priceL;
    Qty size;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"

    Order(bool isStrategy, OrderId orderId, Side side, Qty size, PriceL price) : isStrategy{isStrategy},
                                                                                 orderId{orderId}, side{side},
                                                                                 size{size},
                                                                                 priceL{price} {
        assert(side == Side::BUY || side == Side::SELL);
        assert(size > 0);
        assert(priceL > 0);
    }

#pragma clang diagnostic pop

};


struct L3 {

    using OrderMap = map<OrderId, Order>;

    const Side side;
    Qty levelSize;
    OrderMap orders;

    using const_iterator = OrderMap::const_iterator;
    using iterator = OrderMap::iterator;

    explicit L3(OrderMap &&mp, Side side) : orders(mp), levelSize{0}, side{side} {
        assert(side == Side::BUY || side == Side::SELL);
        for (const auto &it: mp) {
            levelSize += it.second.size;
        }
    }

    [[nodiscard]] bool empty() const {
        assert(orders.empty() == (levelSize == 0));
        return orders.empty();
    }

    // TODO - use compile time if here
    [[nodiscard]] const_iterator begin() const {
        assert(!orders.empty());
        return orders.begin();
    }

    iterator begin() {
        assert(!orders.empty());
        return orders.begin();
    }

    [[nodiscard]] const_iterator end() const {
        assert(!orders.empty());
        return orders.end();
    }

    iterator end() {
        assert(!orders.empty());
        return orders.end();
    }

    iterator find(OrderId id) {
        assert(!orders.empty());
        const iterator &res = orders.find(id);
        assert(res != orders.end());
        return res;
    }

    pair<iterator, bool> emplace(OrderId orderId, Order order) {
        assert(!orders.empty());
        assert(orders.find(orderId) == orders.end());
        assert(order.orderId == orderId && order.size > 0);
        assert(order.side == side);
        levelSize += order.size;

        return orders.emplace(orderId, order);
    }

    void removeQty(iterator iterator, Qty qty) {
        assert(qty <= iterator->second.size);
        iterator->second.size -= qty;
        levelSize -= qty;
    }

    void erase(iterator iter) {
        assert(orders.begin()->second.priceL == iter->second.priceL);

        levelSize -= iter->second.size;
        orders.erase(iter);

        assert(levelSize >= 0);
    }

    [[nodiscard]] size_t size() const {
        assert(levelSize > 0);
        assert(!orders.empty());
        return orders.size();
    }

    Qty match(Qty remainingQty, vector<OrderId> &toDel, vector<InboundMsg::Trade> &trades) {
        assert(remainingQty > 0);
        Qty ogQty = remainingQty;
        auto it = orders.begin();
        for (; it != orders.end() && remainingQty > 0; ++it) {
            Qty qtyAtLevel = it->second.size;
            OrderId orderId = it->second.orderId;
            PriceL price = it->second.priceL;
            Qty matchedQty = min(qtyAtLevel, remainingQty);
            if (matchedQty < qtyAtLevel) {
                removeQty(it, remainingQty);
                remainingQty = 0;
                trades.emplace_back(orderId, price, matchedQty);
                break;
            } else {
                trades.emplace_back(orderId, price, qtyAtLevel);
                toDel.push_back(orderId);
            }
            remainingQty -= it->second.size;
        }
        assert(remainingQty < ogQty || it != orders.begin());
        assert(remainingQty >= 0);
        return remainingQty;
    }

private:
    void stateChecks() {
        // check that the levelSize matches the sum of the orders
        // check that all orders within a level have the same price and side and different order ids.
        // check that the size > 0 for all orders
        // all keys match the order id of the orders
        // check that all sizes and prices are positive.
        // assert that we iterate over the orders in price-time priority
    }

};

using SideLevels = list<L3>;

struct OrderIdLoc {
    Side side;
    SideLevels::iterator l2;
    L3::iterator loc;

    OrderIdLoc(Side side, SideLevels::iterator l2, L3::iterator loc) : side{side}, l2{l2}, loc{loc} {
    }

};


class L3OrderBook {

    map<OrderId, OrderIdLoc> orderIdMap;
    SideLevels bidLevels;
    SideLevels askLevels;
    OrderId nextId;

    static constexpr OrderId STRATEGY_ORDER_ID_START = OrderId(1) << 44;


public:
    InboundMsg::TopLevelUpdate cached;
    TimeNs lastUpdateTs;

    L3OrderBook() : lastUpdateTs{0}, bidLevels(), askLevels(), orderIdMap(), nextId{STRATEGY_ORDER_ID_START}, cached{} {
    }

    [[nodiscard]] bool empty() const noexcept {
        return orderIdMap.empty();
    }

    OrderId nextOrderId() {
        return nextId++;
    }

    bool bboUpdated() {
        const InboundMsg::TopLevelUpdate &curBBO = getBBO();
        bool updated = !(cached == curBBO);
        cached = curBBO;
        return updated;
    }


    std::vector<InboundMsg::Trade>
    submit(TimeNs t, bool isStrategy, OrderId orderId, Qty size, Side side, // NOLINT(*-no-recursion)
           PriceL priceL) noexcept { // NOLINT(*-no-recursion)
        assert(orderId < STRATEGY_ORDER_ID_START || isStrategy);
        stateCheck();
        lastUpdateTs = t;
        SideLevels &levelsOpp = getOppSideLevels(side);
        if (isAggressiveOrder(priceL, levelsOpp, side)) {
            const auto &[remainingSize, trades] = match(t, levelsOpp, side, size, priceL);
            if (remainingSize > 0) {
                assert(!isAggressiveOrder(priceL, levelsOpp, side));
                auto ts = submit(t, isStrategy, orderId, remainingSize, side, priceL);
                assert(ts.empty());
            }
            return trades;
        } else {
            insertLevel(isStrategy, orderId, side, size, priceL);
            return {};
        }
    }

    void modify(TimeNs t, OrderId oldOrderId, Qty newSize) {
        stateCheck();
        lastUpdateTs = t;
        const Order existingOrder = cancel(lastUpdateTs, oldOrderId);
        assert(existingOrder.orderId == oldOrderId);
        assert(existingOrder.size != newSize);
        submit(lastUpdateTs, existingOrder.isStrategy, oldOrderId, newSize, existingOrder.side, existingOrder.priceL);
    }

    Order cancel(TimeNs t, OrderId orderId) {
        stateCheck();
        lastUpdateTs = t;
        const auto &elem = orderIdMap.find(orderId);
        assert(elem != orderIdMap.end());
        const auto &[id, loc] = *elem;

        auto &l2 = loc.l2;
        const Order deletedOrder = loc.loc->second;
        l2->erase(loc.loc);
        if (l2->empty()) {
            getSideLevels(elem->second.side).erase(l2);
        }
        orderIdMap.erase(orderId);

        assert(deletedOrder.orderId == orderId);

        return deletedOrder;
    }


    Qty getNumLevels(Side side) {
        return Qty(getSideLevels(side).size());
    }

    double getLevelPrice(Side side, int level) {

        SideLevels &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);
        if (iter == levels.end()) {
            return std::nan("");
        }
        return getPriceD(*iter);
    }

    Qty getLevelSize(Side side, int level) {
        SideLevels &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);

        if (iter == levels.end()) {
            return 0;
        }
        const auto &l3 = *iter;
        return l3.levelSize;
    }

    int getLevelOrderCount(Side side, int level) {
        SideLevels &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);
        if (iter == levels.end()) {
            return 0;
        }
        return int(iter->size());
    }


private:
    static PriceL getPriceL(const L3 &bids) noexcept {
        assert(!bids.empty());
        return bids.begin()->second.priceL;
    }

    static double getPriceD(const L3 &bids) noexcept {
        assert(!bids.empty());
        return round(double(bids.begin()->second.priceL) / double(PRECISION / 100)) / 100; // NOLINT(*-integer-division)
    }

    static bool isAggressiveOrder(PriceL price, SideLevels &levels, Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        int dir = side == Side::BUY ? 1 : -1;
        return !levels.empty() && (getPriceL(*levels.begin()) - price) * dir <= 0;
    }

    static bool isBid(Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY;
    }

    void insertLevel(bool isStrategy, OrderId orderId, Side side, Qty size, PriceL priceL) noexcept {

        SideLevels &levels = getSideLevels(side);

        assert(size >= 0 && priceL >= 0);

        auto compare = [side](PriceL a, PriceL b) {
            if (side == Side::BUY) {
                return a > b;
            } else {
                return a < b;
            }
        };
        auto iter = levels.begin();
        for (; iter != levels.end(); ++iter) {
            PriceL curPrice = getPriceL(*iter);
            if (compare(priceL, curPrice)) {

                auto insertPos = levels.emplace(iter,
                                                L3{{{orderId, Order{isStrategy, orderId, side, size, priceL}}}, side});
                auto res = insertPos->find(orderId);
                orderIdMap.emplace(orderId, OrderIdLoc{side, insertPos, res});
                return;
            } else if (priceL == curPrice) {
                auto res = iter->emplace(orderId, Order{isStrategy, orderId, side, size, priceL});
                if (!res.second) {
                    assert(false);
                }
                orderIdMap.emplace(orderId, OrderIdLoc{side, iter, res.first});
                return;
            }
        }

        assert(orderIdMap.find(orderId) == orderIdMap.end());
        assert(iter == levels.end());
        const SideLevels::iterator &insertPos = levels.emplace(iter, L3{{{orderId,
                                                                          Order{isStrategy, orderId, side, size,
                                                                                priceL}}},
                                                                        side});
        const auto &res = insertPos->find(orderId);
        orderIdMap.emplace(orderId, OrderIdLoc{side, insertPos, res});

    }

    static bool isAggPrice(Side side, PriceL priceL, PriceL restingPrice) noexcept {
        if (isBid(side)) {
            return priceL >= restingPrice;
        } else {
            return priceL <= restingPrice;
        }
    }

    pair<Qty, vector<InboundMsg::Trade>>
    match(TimeNs t, SideLevels &oppLevels, Side side, Qty size, PriceL priceL) noexcept {
        assert(isAggressiveOrder(priceL, oppLevels, side));
        Qty remainingQty = size;
        auto levelIter = oppLevels.begin();
        static vector<OrderId> orderIDsToDelete;
        orderIDsToDelete.reserve(8);

        vector<InboundMsg::Trade> trades;

        while (remainingQty > 0 && levelIter != oppLevels.end() && isAggPrice(side, priceL, getPriceL(*levelIter))) {
            remainingQty = levelIter->match(remainingQty, orderIDsToDelete, trades);
            ++levelIter;
        }
        for (auto orderID: orderIDsToDelete) {
            const Order existingOrder = cancel(t, orderID);
            assert(isAggPrice(side, priceL, existingOrder.priceL));
        }
        assert(levelIter == oppLevels.end() || !levelIter->empty() && remainingQty == 0);
        assert(remainingQty == 0 || !isAggressiveOrder(priceL, oppLevels, side));
        return {remainingQty, trades};
    }

    /*
     *     template <bool IsConst = false>
    auto getValue() -> std::conditional_t<IsConst, const int&, int&> {
        if constexpr (IsConst) {
            std::cout << "const version\n";
            return value;
        } else {
            std::cout << "non-const version\n";
            return value;
        }
    }

     */

    SideLevels &getSideLevels(Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY ? bidLevels : askLevels;
    }

    SideLevels &getOppSideLevels(Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY ? askLevels : bidLevels;
    }

    static SideLevels::iterator findLevel(SideLevels &levels, int level) noexcept {
        assert(level >= 0);
        assert(!levels.empty());
        auto iter = levels.begin();
        for (int i = 0; iter != levels.end() && i < level; ++i, ++iter);
        return iter;
    }

    InboundMsg::TopLevelUpdate getBBO() {

        PriceL bidPrice = 0;
        Qty bidSize = -1;
        PriceL askPrice = -1;
        Qty askSize = 0;
        if (!bidLevels.empty()) {
            bidPrice = getPriceL(*bidLevels.begin());
            bidSize = getLevelSize(Side::BUY, 0);
        }
        if (!askLevels.empty()) {
            askPrice = getPriceL(*askLevels.begin());
            askSize = getLevelSize(Side::SELL, 0);
        }

        return InboundMsg::TopLevelUpdate{bidPrice, bidSize, askPrice, askSize};
    }

    void stateCheck() {
        // check that across the book there is only a single mention of an order
        // check that the levels are ordered by price
        // check that the bbo is valid
        // lastUpdateTs is always increasing.
        // the set of the orderidmap matches the set of the bidlevels and sidelevels.
    }

};


#endif //HFT_L3ORDERBOOK_H
