
#include <stdio.h>
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

using namespace std;

/*
 * Differences from submitted solution:
 *
 * Fixes bug 2 bugs and adds more checks.
 * Handles aggressive orders and caches levelSize calculation.
 */

/*
    OrderBook is a level 3 orderbook.  Please fill out the stub below.
*/

using u64 = uint64_t;
using i64 = int64_t;

i64 PRECISION = 1e9;

struct Order {
    const int orderId;
    const char side;
    i64 priceL;
    int size;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"

    Order(int orderId, char side, int size, i64 price) : orderId{orderId}, side{side}, size{size}, priceL{price} {
        assert(side == 'b' || side == 's');
        assert(size > 0);
        assert(priceL > 0);
    }

#pragma clang diagnostic pop

};

using OrderId = int;


struct L3 {
    const char side;

    int levelSize;
    map<OrderId, Order> orders;

    using const_iterator = map<OrderId, Order>::const_iterator;
    using iterator = map<OrderId, Order>::iterator;

    explicit L3(map<OrderId, Order> &&mp, char side) : orders(mp), levelSize{0}, side{side} {
        assert(side == 'b' || side == 's');
        for (const auto &it: mp) {
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
        const iterator &res = orders.find(id);
        assert(res != orders.end());
        return res;
    }

    pair<iterator, bool> emplace(int orderId, Order order) {
        assert(!orders.empty());
        assert(orders.find(orderId) == orders.end());
        assert(order.orderId == orderId && order.size > 0);
        assert(order.side == side);
        levelSize += order.size;

        return orders.emplace(orderId, order);
    }

    void removeQty(iterator iterator, int qty) {
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

    int match(int remainingQty, vector<OrderId> &toDel) {
        assert(remainingQty > 0);
        int ogQty = remainingQty;
        auto it = orders.begin();
        for (; it != orders.end() && remainingQty > 0; ++it) {
            int qtyAtLevel = it->second.size;
            int orderId = it->second.orderId;
            int matchedQty = min(qtyAtLevel, remainingQty);
            if (matchedQty < qtyAtLevel) {
                removeQty(it, remainingQty);
                remainingQty = 0;
                break;
            } else {
                toDel.push_back(orderId);
            }
            remainingQty -= it->second.size;
        }
        assert(remainingQty < ogQty || it != orders.begin());
        assert(remainingQty >= 0);
        return remainingQty;
    }

    void reduce(iterator pos, int newSize) {
        assert(levelSize > 0);
        assert(!orders.empty());
        levelSize -= (pos->second.size - newSize);
        pos->second.size = newSize;
        assert(levelSize >= 0);
    }
};


//using L3 = map<OrderId, Order>;

using SideLevels = list<L3>;

struct OrderIdLoc {
    char side;
    SideLevels::iterator l2;
    L3::iterator loc;

    OrderIdLoc(char side, SideLevels::iterator l2, L3::iterator loc) : side{side}, l2{l2}, loc{loc} {
    }

};

class OrderBook {
    map<OrderId, OrderIdLoc> orderIdMap;
    SideLevels bidLevels;
    SideLevels askLevels;

public:

    OrderBook() : bidLevels(), askLevels(), orderIdMap() {
    }

    static i64 getPriceL(const L3 &bids) {
        assert(!bids.empty());
        return bids.begin()->second.priceL;
    }

    static double getPriceD(const L3 &bids) {
        assert(!bids.empty());
        return round(double(bids.begin()->second.priceL) / double(PRECISION / 100)) / 100;
    }

    static bool isAggressiveOrder(i64 price, SideLevels &levels, char side) {
        assert(side == 'b' || side == 's');
        int dir = side == 'b' ? 1 : -1;
        return !levels.empty() && (getPriceL(*levels.begin()) - price) * dir <= 0;
    }

    static i64 normPrice(double price) {
        return static_cast<i64>(static_cast<double>(PRECISION) * price);
    }

    static bool isBid(char side) {
        assert(side == 'b' || side == 's');
        return side == 'b';
    }

    void insertLevel(int orderId, char side, int size, i64 priceL) {

        SideLevels &levels = side == 'b' ? bidLevels : askLevels;

        assert(size >= 0 && priceL >= 0);

        auto compare = [side](i64 a, i64 b) {
            if (side == 'b') {
                return a > b;
            } else {
                return a < b;
            }
        };
        auto iter = levels.begin();
        for (; iter != levels.end(); ++iter) {
            i64 curPrice = getPriceL(*iter);
            if (compare(priceL, curPrice)) {

                auto insertPos = levels.emplace(iter, L3{{{orderId, Order{orderId, side, size, priceL}}}, side});
                auto res = insertPos->find(orderId);
                orderIdMap.emplace(orderId, OrderIdLoc{side, insertPos, res});
                return;
            } else if (priceL == curPrice) {
                auto res = iter->emplace(orderId, Order{orderId, side, size, priceL});
                if (!res.second) {
                    assert(false);
                }
                orderIdMap.emplace(orderId, OrderIdLoc{side, iter, res.first});
                return;
            }
        }

        assert(iter == levels.end());
        const SideLevels::iterator &insertPos = levels.emplace(iter, L3{{{orderId, Order{orderId, side, size, priceL}}},
                                                                        side});
        const auto &res = insertPos->find(orderId);
        orderIdMap.emplace(orderId, OrderIdLoc{side, insertPos, res});

    }

    static bool isAggPrice(char side, i64 priceL, i64 restingPrice) {
        if (isBid(side)) {
            return priceL >= restingPrice;
        } else {
            return priceL <= restingPrice;
        }
    }

    int match(SideLevels &oppLevels, char side, int size, i64 priceL) {
        assert(isAggressiveOrder(priceL, oppLevels, side));
        int remainingQty = size;
        auto levelIter = oppLevels.begin();
        static vector<OrderId> orderIDsToDelete;
        orderIDsToDelete.reserve(8);

        while (remainingQty > 0 && levelIter != oppLevels.end() && isAggPrice(side, priceL, getPriceL(*levelIter))) {
            remainingQty = levelIter->match(remainingQty, orderIDsToDelete);
            ++levelIter;
        }
        for (auto orderID: orderIDsToDelete) {
            const Order existingOrder = deleteOrder(orderID);
            assert(isAggPrice(side, priceL, existingOrder.priceL));
        }
        assert(levelIter == oppLevels.end() || !levelIter->empty() && remainingQty == 0);
        assert(remainingQty == 0 || !isAggressiveOrder(priceL, oppLevels, side));
        return remainingQty;
    }

    //adds order to order book
    void newOrder(int orderId, char side, int size, double priceD) { // NOLINT(*-no-recursion)
        SideLevels &levelsOpp = getOppSideLevels(side);
        i64 priceL = normPrice(priceD);
        if (isAggressiveOrder(priceL, levelsOpp, side)) {
            int remainingSize = match(levelsOpp, side, size, priceL);
            if (remainingSize > 0) {
                assert(!isAggressiveOrder(priceL, levelsOpp, side));
                newOrder(orderId, side, remainingSize, priceD);
            }
        } else {
            insertLevel(orderId, side, size, priceL);
        }
    }


    void modifyOrder(int oldOrderId, int orderId, char side, int newSize, double newPrice) {
        const Order existingOrder = deleteOrder(oldOrderId);
        assert(existingOrder.side == side || side != 'b' && side != 's');
        assert(existingOrder.orderId == oldOrderId);
        assert(existingOrder.priceL != normPrice(newPrice) || existingOrder.size != newSize);
        newOrder(orderId, existingOrder.side, newSize, newPrice);
    }

    //replaces order with different order
    void reduceOrder(int orderId, int newSize) {
        const auto &elem = orderIdMap.find(orderId);
        assert(elem != orderIdMap.end());
        auto &pos = elem->second;
        if (pos.loc->second.size >= newSize) {
            pos.l2->reduce(pos.loc, newSize);
            if (pos.loc->second.size <= 0) {
                const Order res = deleteOrder(orderId);
                assert(res.size == 0);
                assert(res.orderId == orderId);
            }
        }

    }

    //deletes order from orderbook
    Order deleteOrder(int orderId) {
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


    //returns the number of levels on the side
    int getNumLevels(char side) {
        return int(getSideLevels(side).size());
    }

    //returns the price of a level.  Level 0 is top of book.
    double getLevelPrice(char side, int level) {

        SideLevels &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);
        if (iter == levels.end()) {
            return std::nan("");
        }
        return getPriceD(*iter);
    }

    //returns the size of a level.
    int getLevelSize(char side, int level) {
        // TODO - optimize this.
        SideLevels &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);

        if (iter == levels.end()) {
            return 0;
        }
        const auto &l3 = *iter;
        return l3.levelSize;
    }

    //returns the number of orders contained in price level.
    int getLevelOrderCount(char side, int level) {
        SideLevels &levels = getSideLevels(side);
        auto iter = findLevel(levels, level);
        if (iter == levels.end()) {
            return 0;
        }
        return int(iter->size());
    }

    SideLevels &getSideLevels(char side) {
        assert(side == 'b' || side == 's');
        return side == 'b' ? bidLevels : askLevels;
    }

    SideLevels &getOppSideLevels(char side) {
        assert(side == 'b' || side == 's');
        return side == 'b' ? askLevels : bidLevels;
    }

    static SideLevels::iterator findLevel(SideLevels &levels, int level) {
        assert(level >= 0);
        auto iter = levels.begin();
        for (int i = 0; iter != levels.end() && i < level; ++i, ++iter);
        return iter;
    }
};

/*
    Do not change main function
*/


int main(int argc, char *argv[]) {
    char instruction;
    char side;
    int orderId;
    double price;
    int size;
    int oldOrderId;

//    FILE *file = fopen(std::getenv("OUTPUT_PATH"), "w");
    FILE *file = fopen("/home/jc/CLionProjects/hft/data/output.txt", "w");

    OrderBook book;
    int counter = 0;
    while (scanf("%c %c %d %d %lf %d\n", &instruction, &side, &orderId, &size, &price, &oldOrderId) != EOF) {
        ++counter;
        switch (instruction) {
            case 'n':
                book.newOrder(orderId, side, size, price);
                break;
            case 'r':
                book.reduceOrder(orderId, size);
                break;
            case 'm':
                book.modifyOrder(oldOrderId, orderId, side, size, price);
                break;
            case 'd':
                book.deleteOrder(orderId);
                break;
            case 'p':
                fprintf(file, "%.02lf\n", book.getLevelPrice(side, orderId));
                break;
            case 's':
                fprintf(file, "%d\n", book.getLevelSize(side, orderId));
                break;
            case 'l':
                fprintf(file, "%d\n", book.getNumLevels(side));
                break;
            case 'c':
                fprintf(file, "%d\n", book.getLevelOrderCount(side, orderId));
                break;
            default:
                fprintf(file, "invalid input\n");
        }
    }
    fclose(file);
    return 0;
}
