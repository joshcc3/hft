//
// Created by jc on 24/10/23.
//

#ifndef HFT_STRATEGY_H
#define HFT_STRATEGY_H

#include "mytypedefs.h"

using u64 = uint64_t;

#include <iostream>
#include <cmath>
#include <cstdint>
#include <cassert>

// TODO - need to request the current time from the simulation

class Strategy {
private:
    // Enum for the side of the order
    enum class Side {
        BUY, SELL, NUL
    };


    // Parameters
    const double alpha;
    const double thresholdBps;
    const PriceL maxNotional; // Max inventory notional

    // State variables

    OrderId nextOrderId;

    enum class State {
        IDLE,
        WAITING
    } state = State::IDLE;
    OrderId openOrderId;
    Side openOrderSide;
    Qty openOrderQty;

    NotionalL inventoryNotional; // Keeping as signed to handle both long and short inventory

    PriceL bestBidPrice;
    Qty bestBidSize;
    PriceL bestAskPrice;
    Qty bestAskSize;
    PriceL theoreticalValue;

public:
    Strategy(double alphaValue, double thresholdValueBps, double maxNotional)
            : alpha(alphaValue), thresholdBps(thresholdValueBps),
              maxNotional{static_cast<PriceL>(maxNotional * std::pow(10, 9))},
              bestBidPrice(0),
              bestBidSize(-1), bestAskPrice(0), bestAskSize(-1), theoreticalValue(0), inventoryNotional(0),
              openOrderId{-1}, openOrderSide{Side::NUL}, openOrderQty{-1}, nextOrderId{1} {

        assert(alphaValue >= 0 && alphaValue <= 1.0);
        assert(thresholdValueBps >= 0 && thresholdValueBps <= 1000.0);
    }

    // public messages
    void onTopLevelUpdate(PriceL bidPrice, Qty bidSize, PriceL askPrice, Qty askSize) {
        assert(bidPrice < askPrice);
        assert(bidSize > 0 && askSize > 0);
        assert(bidPrice < 100000000 / bidSize);
        assert(askPrice < 100000000 / askSize);
        assert(abs(bestBidPrice - bidPrice) / double(bestBidPrice) < 0.1);
        assert(abs(bestAskPrice - askPrice) / double(bestAskPrice) < 0.1);
        assert(bidPrice < bestAskPrice);
        assert(askPrice > bestBidPrice);
        // either both dont change or there was a trade
        assert(((bidPrice == bestBidPrice && bidSize == bestBidSize) !=
                (askPrice == bestAskPrice && askSize == bestAskSize)) ||
               (bidPrice < bestBidPrice || askPrice < bestAskPrice));

        stateChecks();

        bestBidPrice = bidPrice;
        bestBidSize = bidSize;
        bestAskPrice = askPrice;
        bestAskSize = askSize;

        // Calculate VWAP
        PriceL vwap = (bestBidPrice * bestAskSize + bestAskPrice * bestBidSize) / (bestBidSize + bestAskSize);

        // Update the theoretical value using EWMA
        theoreticalValue = static_cast<PriceL>(alpha * double(vwap) + (1.0 - alpha) * theoreticalValue);

        decideTrade();
    }

    void orderModified(OrderId id, Qty newQty) {
        assert(false);
    }

    void orderAccepted(OrderId id) {
        stateChecks();
        assert(state == State::WAITING);
        assert(openOrderId == id);

        submitCancel(id);
    }

    void orderCancelled(OrderId id) {
        stateChecks();
        assert(state == State::WAITING);
        assert(openOrderId == id);

        orderComplete();
    }

    void trade(OrderId id, PriceL price, Qty qty) {
        stateChecks();
        assert(price <= 1e6);
        assert(qty <= 1e6);
        assert(state == State::WAITING);
        assert(openOrderId == id);

        if (openOrderSide == Side::BUY) {
            inventoryNotional += NotionalL(price * qty);
        } else {
            inventoryNotional -= NotionalL(price * qty);
        }

        openOrderQty -= qty;

        if (openOrderQty == 0) {
            orderComplete();
        }

        stateChecks();
    }

#pragma clang diagnostic pop

    void submitOrder(Side side, PriceL price, Qty size) {
        std::cout << "Order Submitted: " << (side == Side::BUY ? "BUY" : "SELL") << " " << size << " @ "
                  << static_cast<double>(price) * 1e-9 << std::endl;

        assert(state == State::IDLE);
        assert((maxNotional - inventoryNotional) / size >= price);
        assert(side == Side::BUY && price >= bestAskPrice || side == Side::SELL && price <= bestBidPrice);

        state = State::WAITING;
        openOrderId = nextOrderId++;
        openOrderSide = side;
        openOrderQty = size;

        stateChecks();
    }

    void submitCancel(OrderId orderId) {
        assert(state == State::WAITING);
        assert(openOrderId == orderId);

        std::cout << "Order Cancel: " << orderId << std::endl;
    }

private:

    void stateChecks() {
        assert((openOrderId == -1) == (state == State::IDLE) == (openOrderSide == Side::NUL) == (openOrderQty == 0));
        assert(openOrderQty >= 0);
        assert(bestBidPrice < bestAskPrice);
        assert(abs(inventoryNotional) < maxNotional);
    }

    void orderComplete() {
        state = State::IDLE;
        openOrderId = -1;
        openOrderSide = Side::NUL;
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"

    void decideTrade() {
        assert(maxNotional >= abs(inventoryNotional));
        if (state == State::IDLE) {
            doTrade();
            assert(state == State::WAITING);
        }
        assert(maxNotional >= abs(inventoryNotional));
    }

    void doTrade() {// Check for buy signal
        if (theoreticalValue > static_cast<u64>(bestAskPrice * (1.0 + thresholdBps))) {
            u64 targetSize = std::min(uint64_t(bestAskSize), (maxNotional - inventoryNotional) / bestAskPrice);
            if (targetSize > 0) {
                submitOrder(Side::BUY, bestAskPrice, targetSize);
            }
        } else if (theoreticalValue < bestBidPrice * (1.0 - thresholdBps)) {
            u64 targetSize = std::min(uint64_t(bestBidSize), (maxNotional + inventoryNotional) / bestBidPrice);
            if (targetSize > 0) {
                submitOrder(Side::SELL, bestBidPrice, targetSize);
            }
        }
    }

};

int main() {
    // Example usage:
    Strategy strategy(0.9, 0.005);  // Using 0.9 as alpha for EWMA and 0.005 for 5 bps thresholdBps

    // Mock market data updates (multiplied by 10^9 for fixed-point representation)
    strategy.onTopLevelUpdate(100'000'000'000, 50, 101'000'000'000, 50);
    strategy.onTopLevelUpdate(100'500'000'000, 50, 101'500'000'000, 50);
    strategy.onTopLevelUpdate(101'000'000'000, 50, 102'000'000'000, 50);
    return 0;
}


#endif //HFT_STRATEGY_H
