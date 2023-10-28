#ifndef HFT_L3ORDERBOOK_H
#define HFT_L3ORDERBOOK_H

#define NDEBUG

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <list>
#include <iostream>
#include <vector>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <array>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include "mytypedefs.h"




struct Order {
    PriceL priceL;
    Qty size;
    OrderId orderId;
    bool isStrategy;
    Side side;
    TimeNs timeNs;


    Order(TimeNs timeNs, bool isStrategy, OrderId orderId, Side side, Qty size, PriceL price) : isStrategy{isStrategy},
                                                                                                orderId{orderId},
                                                                                                side{side},
                                                                                                size{size},
                                                                                                priceL{price},
                                                                                                timeNs{timeNs} {
        assert(side == Side::BUY || side == Side::SELL);
        assert(size > 0);
        assert(priceL > 0);
    }


};

class L3Vec {

public:
    using OrderMap = std::vector<std::pair<OrderId, Order>>;
    using const_iterator = OrderMap::const_iterator;
    using iterator = OrderMap::iterator;

    OrderMap orders;
    Qty levelSize;
    const Side side;

    explicit L3Vec(OrderMap &&mp, Side side) : orders(mp), levelSize{0}, side{side} {
        assert(side == Side::BUY || side == Side::SELL);
        orders.reserve(10);
        for (const auto &it: orders) {
            levelSize += it.second.size;
        }
    }

    [[nodiscard]] bool empty() const {
        assert(orders.empty() == (levelSize == 0));
        return orders.empty();
    }

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
        for (int i = 0; i < orders.size(); ++i) {
            if (orders[i].first == id) {
                return orders.begin() + i;
            }
        }
        assert(false);
    }

    iterator emplace(OrderId orderId, Order order) {
        assert(!orders.empty());
        assert(find_if(orders.begin(), orders.end(), [orderId](const auto &p) { return p.first == orderId; }) ==
               orders.end());
        assert(order.orderId == orderId && order.size > 0);
        assert(order.side == side);
        levelSize += order.size;

        orders.emplace_back(orderId, order);
        assert(stateChecks());
        return orders.end() - 1;
    }

    void removeQty(iterator iterator, Qty qty) {
        assert(qty <= iterator->second.size);
        iterator->second.size -= qty;
        levelSize -= qty;
        assert(stateChecks());

    }

    void erase(iterator iter) {
        assert(orders.begin()->second.priceL == iter->second.priceL);

        levelSize -= iter->second.size;

        orders.erase(iter);

        assert(stateChecks());

        assert(levelSize >= 0);
    }

    [[nodiscard]] size_t size() const {
        assert(levelSize > 0);
        assert(!orders.empty());
        return orders.size();
    }

    Qty match(Qty remainingQty, std::vector<OrderId> &toDel, std::vector<InboundMsg> &trades) {
        assert(stateChecks());
        assert(remainingQty > 0);
        const Qty ogQty = remainingQty;
        auto it = orders.begin();
        for (; it != orders.end() && remainingQty > 0; ++it) {
            const Qty qtyAtLevel = it->second.size;
            const OrderId orderId = it->second.orderId;
            const PriceL price = it->second.priceL;
            Qty matchedQty = std::min(qtyAtLevel, remainingQty);
            if (matchedQty < qtyAtLevel) {
                removeQty(it, remainingQty);
                remainingQty = 0;
                trades.emplace_back(InboundMsg{InboundMsg::Trade{orderId, price, matchedQty}});
                break;
            } else {
                trades.emplace_back(InboundMsg{InboundMsg::Trade{orderId, price, qtyAtLevel}});
                toDel.push_back(orderId);
            }
            remainingQty -= it->second.size;
        }
        assert(remainingQty < ogQty || it != orders.begin());
        assert(remainingQty >= 0);
        assert(stateChecks());
        return remainingQty;
    }

private:

    bool stateChecks() {
        // check that the levelSize matches the sum of the orders
        // check that all orders within a level have the same price and side and different order ids.
        // check that the size > 0 for all orders
        // all keys match the order id of the orders
        // check that all sizes and prices are positive.
        // assert that we iterate over the orders in price-time priority

        bool check = true;
        int sum = std::accumulate(orders.begin(), orders.end(), 0,
                                  [](int acc, const std::pair<OrderId, Order> &p) { return acc + p.second.size; });
        assert(check &= sum == levelSize);

        bool samePriceSide = orders.empty() || std::accumulate(orders.begin(), orders.end(), true,
                                                               [this](bool acc, const std::pair<OrderId, Order> &p) {
                                                                   return acc &&
                                                                          p.second.priceL == orders[0].second.priceL
                                                                          && p.second.side == orders[0].second.side;
                                                               });
        assert(check &= samePriceSide);
        bool sizesPricesValid = orders.empty() || std::accumulate(orders.begin(), orders.end(), true,
                                                                  [](bool acc, const std::pair<OrderId, Order> &p) {
                                                                      return acc &&
                                                                             p.second.priceL > 0
                                                                             && p.second.size > 0;
                                                                  });
        assert(check &= sizesPricesValid);
        std::unordered_set<OrderId> os;
        transform(orders.begin(), orders.end(), std::inserter(os, os.end()), [](const auto &p) { return p.first; });
        assert(check &= os.size() == orders.size());

        bool orderValid = orders.empty() || std::accumulate(orders.begin(), orders.end(), true,
                                                            [](bool acc, const std::pair<OrderId, Order> &p) {
                                                                return acc && p.first == p.second.orderId;
                                                            });
        assert(check &= orderValid);

        std::vector<TimeNs> times;
        transform(orders.begin(), orders.end(), std::inserter(times, times.end()),
                  [](const auto &p) { return p.second.timeNs; });

        assert(check &= is_sorted(times.begin(), times.end()));

        return check;

    }

};

template<typename T>
using SideLevels = std::list<T>;

template<typename L3>
class OrderIdLoc;


template<typename L3>
struct OrderIdLoc {
    Side side;
    OrderId orderId;
    typename SideLevels<L3>::iterator l2;
    typename L3::iterator loc;

    OrderIdLoc(OrderId orderId, Side side, typename SideLevels<L3>::iterator l2, typename L3::iterator loc) : orderId{
            orderId}, side{side}, l2{l2},
                                                                                                              loc{loc} {
    }

    [[nodiscard]] typename L3::iterator getOrder() const {
        return loc;
    }

};

template<>
struct OrderIdLoc<L3Vec> {

    OrderId orderId;
    Side side;
    typename SideLevels<L3Vec>::iterator l2;


    OrderIdLoc(OrderId orderId, Side side, typename SideLevels<L3Vec>::iterator l2) : orderId{
            orderId}, side{side}, l2{l2} {
    }

    [[nodiscard]] L3Vec::iterator getOrder() const {
        int ix;
        L3Vec &v = *l2;
        for (ix = 0; ix < l2->size() && v.orders[ix].first != orderId; ++ix);
        assert(ix < l2->size() && v.orders[ix].first == orderId);
        return v.begin() + ix;
    }

};


template<typename L3>
class L3OrderBook {

    std::unordered_map<OrderId, OrderIdLoc<L3>> orderIdMap;
    SideLevels<L3> bidLevels;
    SideLevels<L3> askLevels;
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
        const bool updated = !(cached == curBBO);
        cached = curBBO;
        return updated;
    }


    std::vector<InboundMsg>
    submit(TimeNs t, bool isStrategy, OrderId orderId, Qty size, Side side, // NOLINT(*-no-recursion)
           PriceL priceL) noexcept { // NOLINT(*-no-recursion)
        assert(orderId < STRATEGY_ORDER_ID_START || isStrategy);
        assert(orderIdMap.find(orderId) == orderIdMap.end());
        assert(stateCheck());
        lastUpdateTs = t;
        SideLevels<L3> &levelsOpp = getOppSideLevels(side);
        if (!isAggressiveOrder(priceL, levelsOpp, side)) {
            {
                auto _s = std::chrono::system_clock::now();
                int i = 2;
                insertLevel(t, isStrategy, orderId, side, size, priceL);
                auto _e = std::chrono::system_clock::now();
                timeSpent[i] += elapsed(_s, _e);
                return {InboundMsg{InboundMsg::OrderAccepted{orderId}}};

            };

        } else {
            auto [remainingSize, trades] = match(t, levelsOpp, side, size, priceL);
            trades.emplace_back(InboundMsg{InboundMsg::Trade{orderId, priceL, size - remainingSize}});
            if (remainingSize > 0) {
                assert(!isAggressiveOrder(priceL, levelsOpp, side));
                const auto &ts = submit(t, isStrategy, orderId, remainingSize, side, priceL);
                assert(ts.size() == 1);
                trades.push_back(ts[0]);
            }
            return move(trades);
        }
    }

    std::vector<InboundMsg> modify(TimeNs t, OrderId oldOrderId, Qty newSize) {
        stateCheck();
        lastUpdateTs = t;

        const auto &existingIter = orderIdMap.find(oldOrderId);
        assert(existingIter != orderIdMap.end() || oldOrderId < STRATEGY_ORDER_ID_START);

        if (existingIter != orderIdMap.end()) {
            Order &existingOrder = existingIter->second.getOrder()->second;
            assert(existingOrder.orderId == oldOrderId);
            assert(existingOrder.size != newSize);
            if (existingOrder.size < newSize) {
                cancel(t, oldOrderId);
                const std::vector<InboundMsg> &output = submit(lastUpdateTs, existingOrder.isStrategy, oldOrderId,
                                                               newSize,
                                                               existingOrder.side, existingOrder.priceL);

                assert(output.size() == 1 && std::holds_alternative<InboundMsg::OrderAccepted>(output[0].content));
            } else {
                existingOrder.size = newSize;
            }
            return {InboundMsg{InboundMsg::OrderModified{oldOrderId, newSize}}};
        } else {
            return {};
        }

    }

    std::vector<InboundMsg> cancel(TimeNs t, OrderId orderId) {
        stateCheck();
        lastUpdateTs = t;
        const auto &elem = orderIdMap.find(orderId);
        if (elem != orderIdMap.end()) {
            assert(elem != orderIdMap.end());
            const auto &[id, loc] = *elem;

            auto &l2 = loc.l2;
            auto iter = loc.getOrder();
            const Order &deletedOrder = iter->second;
            l2->erase(iter);
            if (l2->empty()) {
                getSideLevels(elem->second.side).erase(l2);
            }
            orderIdMap.erase(orderId);

            assert(deletedOrder.orderId == orderId);
            return {InboundMsg{InboundMsg::OrderCancelled{deletedOrder.orderId}}};
        }
        return {};
    }


    [[nodiscard]] Qty getLevelSize(Side side, int level) const {
        const SideLevels<L3> &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);

        if (iter == levels.end()) {
            return 0;
        }
        const auto &l3 = *iter;
        return l3.levelSize;
    }

    static bool isStrategyOrder(OrderId id) {
        return id >= STRATEGY_ORDER_ID_START;
    }

private:
    static PriceL getPriceL(const L3 &bids) noexcept {
        assert(!bids.empty());
        return bids.begin()->second.priceL;
    }

    static bool isAggressiveOrder(PriceL price, SideLevels<L3> &levels, Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        int dir = side == Side::BUY ? 1 : -1;
        return !levels.empty() && (getPriceL(*levels.begin()) - price) * dir <= 0;
    }

    static bool isBid(Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY;
    }

    void insertLevel(TimeNs timeNs, bool isStrategy, OrderId orderId, Side side, Qty size, PriceL priceL) noexcept {

        SideLevels<L3> &levels = getSideLevels(side);

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
                                                L3{{{orderId, Order{timeNs, isStrategy, orderId, side, size, priceL}}},
                                                   side});
//                auto res = insertPos->find(orderId);
                orderIdMap.emplace(orderId, OrderIdLoc<L3>{orderId, side, insertPos});
                return;
            } else if (priceL == curPrice) {
                iter->emplace(orderId, Order{timeNs, isStrategy, orderId, side, size, priceL});
                orderIdMap.emplace(orderId, OrderIdLoc<L3>{orderId, side, iter});
                return;
            }
        }

        assert(orderIdMap.find(orderId) == orderIdMap.end());
        assert(iter == levels.end());
        const typename SideLevels<L3>::iterator &insertPos = levels.emplace(iter, L3{{{orderId,
                                                                                       Order{timeNs, isStrategy,
                                                                                             orderId, side, size,
                                                                                             priceL}}},
                                                                                     side});
//        const auto &res = insertPos->find(orderId);
        orderIdMap.emplace(orderId, OrderIdLoc<L3>{orderId, side, insertPos});

    }

    static bool isAggPrice(Side side, PriceL priceL, PriceL restingPrice) noexcept {
        if (isBid(side)) {
            return priceL >= restingPrice;
        } else {
            return priceL <= restingPrice;
        }
    }

    std::pair<Qty, std::vector<InboundMsg>>
    match(TimeNs t, SideLevels<L3> &oppLevels, Side side, Qty size, PriceL priceL) noexcept {
        assert(isAggressiveOrder(priceL, oppLevels, side));
        Qty remainingQty = size;
        auto levelIter = oppLevels.begin();
        std::vector<OrderId> orderIDsToDelete;
        orderIDsToDelete.reserve(4);

        std::vector<InboundMsg> trades;

        while (remainingQty > 0 && levelIter != oppLevels.end() && isAggPrice(side, priceL, getPriceL(*levelIter))) {
            remainingQty = levelIter->match(remainingQty, orderIDsToDelete, trades);
            ++levelIter;
        }
        for (auto orderID: orderIDsToDelete) {
            PriceL ogPrice = orderIdMap.find(orderID)->second.getOrder()->second.priceL;
            cancel(t, orderID);
            assert(isAggPrice(side, priceL, ogPrice));
        }

        assert(levelIter == oppLevels.end() || !levelIter->empty() && remainingQty == 0);
        assert(remainingQty == 0 || !isAggressiveOrder(priceL, oppLevels, side));
        return {remainingQty, std::move(trades)};
    }

    const SideLevels<L3> &getSideLevels(Side side) const noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY ? bidLevels : askLevels;
    }

    SideLevels<L3> &getSideLevels(Side side) noexcept {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY ? bidLevels : askLevels;
    }

    template<bool IsConst = false>
    auto getOppSideLevels(Side side) noexcept -> std::conditional_t<IsConst, const SideLevels<L3> &, SideLevels<L3> &> {
        assert(side == Side::BUY || side == Side::SELL);
        return side == Side::BUY ? askLevels : bidLevels;
    }

    static typename SideLevels<L3>::const_iterator findLevel(const SideLevels<L3> &levels, int level) noexcept {
        assert(level >= 0);
        assert(!levels.empty());
        auto iter = levels.begin();
        for (int i = 0; iter != levels.end() && i < level; ++i, ++iter);
        return iter;
    }

    [[nodiscard]] InboundMsg::TopLevelUpdate getBBO() const {

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

    bool stateCheck() {
        // check that the bbo is valid
        // lastUpdateTs is always increasing.
        // the set of the orderidmap matches the set of the bidlevels and sidelevels.

        bool check = true;
        std::vector<PriceL> prices;
        prices.reserve(bidLevels.size() + askLevels.size());
        transform(bidLevels.rbegin(), bidLevels.rend(), prices.begin(),
                  [](const L3Vec &vec) { return vec.orders[0].second.priceL; });
        transform(askLevels.begin(), askLevels.end(), prices.begin() + prices.size(),
                  [](const L3Vec &vec) { return vec.orders[0].second.priceL; });
        assert(check &= std::is_sorted(prices.begin(), prices.end()));

        static TimeNs ts = lastUpdateTs;
        assert(check &= ts <= lastUpdateTs);
        ts = lastUpdateTs;

        transform(bidLevels.rbegin(), bidLevels.rend(), prices.begin(),
                  [](const L3Vec &vec) { return vec.orders[0].second.priceL; });
        int allBids = accumulate(bidLevels.begin(), bidLevels.end(), 0,
                                 [](int l, const L3Vec &v) { return l + v.size(); });
        int allOffers = accumulate(askLevels.begin(), askLevels.end(), 0,
                                   [](int l, const L3Vec &v) { return l + v.size(); });
        assert(check &= orderIdMap.size() == allBids + allOffers);

        return check;
    }

};


#endif //HFT_L3ORDERBOOK_H

