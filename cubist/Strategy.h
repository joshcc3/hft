//
// Created by jc on 24/10/23.
//

#ifndef HFT_STRATEGY_H
#define HFT_STRATEGY_H

#include "mytypedefs.h"
#include "L3OrderBook.h"

using u64 = uint64_t;

#include <iostream>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <optional>
#include <functional>
#include <utility>

class Strategy {
    // Enum for the side of the order



    // Parameters
    const double alpha;
    const double thresholdBps;
    const PriceL maxNotional; // Max inventory notional

    // State variables

    std::function<OrderId()> nextOrderId;

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
    Strategy() = delete;

    Strategy(double alphaValue, double thresholdValueBps, double maxNotional, std::function<OrderId()> nextOrderId)
            : alpha(alphaValue), thresholdBps(thresholdValueBps),
              maxNotional{static_cast<PriceL>(maxNotional * std::pow(10, 9))},
              bestBidPrice(0),
              bestBidSize(-1), bestAskPrice(0), bestAskSize(-1), theoreticalValue(-1), inventoryNotional(0),
              openOrderId{-1}, openOrderSide{Side::NUL}, openOrderQty{-1}, nextOrderId{std::move(nextOrderId)} {

        assert(alphaValue >= 0 && alphaValue <= 1.0);
        assert(thresholdValueBps >= 0 && thresholdValueBps <= 1000.0);
    }

    // public messages
    std::optional<OutboundMsg> onTopLevelUpdate(const InboundMsg::TopLevelUpdate &update) {
        if (update.bidPresent() && update.askPresent()) {
            auto [bidPrice, bidSize, askPrice, askSize] = update;
            assert(bidPrice < askPrice);
            assert(bidSize > 0 && askSize > 0);
            assert(bidPrice < 100000000 / bidSize * PRECISION);
            assert(askPrice < 100000000 / askSize * PRECISION);
            assert(bestBidSize <= 0 || abs(bestBidPrice - bidPrice) / double(bestBidPrice) < 0.5);
            assert(bestAskSize <= 0 || abs(bestAskPrice - askPrice) / double(bestAskPrice) < 0.5);
            assert(bestAskSize <= 0 || bidPrice < bestAskPrice);
            assert(bestBidSize <= 0 || askPrice > bestBidPrice);
            // either both dont change or there was a trade
            assert(bestAskSize <= 0 || bestBidSize <= 0 || ((bidPrice == bestBidPrice && bidSize == bestBidSize) !=
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
            theoreticalValue = theoreticalValue > 0 ? static_cast<PriceL>(alpha * double(vwap) + (1.0 - alpha) * theoreticalValue) : vwap;

            return decideTrade();
        } else {
            auto [bidPrice, bidSize, askPrice, askSize] = update;
            bestBidPrice = bidPrice;
            bestBidSize = bidSize;
            bestAskPrice = askPrice;
            bestAskSize = askSize;

            return std::nullopt;
        }
    }

    std::optional<OutboundMsg> orderModified(OrderId id, Qty newQty) {
        assert(false);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<OutboundMsg> orderAccepted(OrderId id) {
        stateChecks();
        assert(state == State::WAITING);
        assert(openOrderId == id);

        return submitCancel(id);
    }

    std::optional<OutboundMsg> orderCancelled(OrderId id) {
        stateChecks();
        assert(state == State::WAITING);
        assert(openOrderId == id);

        orderComplete();

        return std::nullopt;
    }

    std::optional<OutboundMsg> trade(OrderId id, PriceL price, Qty qty) {
        stateChecks();
        assert(price <= 1e6 * PRECISION);
        assert(qty <= 1e6 * PRECISION);

        if (id == openOrderId) {
            assert(state == State::WAITING);
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
        return std::nullopt;
    }

    [[nodiscard]] std::optional<OutboundMsg> submitOrder(Side side, PriceL price, Qty size) {
        std::cout << "Order Submitted: " << (side == Side::BUY ? "BUY" : "SELL") << " " << size << " @ "
                  << static_cast<double>(price) * 1e-9 << "(Theo: " << theoreticalValue/double(PRECISION) << ")" << std::endl;

        assert(state == State::IDLE);
        assert((maxNotional - inventoryNotional) / size >= price);
        assert(side == Side::BUY && price >= bestAskPrice || side == Side::SELL && price <= bestBidPrice);

        state = State::WAITING;
        openOrderId = nextOrderId();
        openOrderSide = side;
        openOrderQty = size;

        stateChecks();

        return std::make_optional<>(OutboundMsg{OutboundMsg::Submit{true, openOrderId, side, price, size}});
    }

    [[nodiscard]] std::optional<OutboundMsg> submitCancel(OrderId orderId) const {
        assert(state == State::WAITING);
        assert(openOrderId == orderId);

        std::cout << "Order Cancel: " << orderId << std::endl;
        return std::make_optional<>(OutboundMsg{OutboundMsg::Cancel{orderId}});
    }

private:

    void stateChecks() {
        assert((openOrderId == -1) == (state == State::IDLE) == (openOrderSide == Side::NUL) == (openOrderQty == -1));
        assert(bestBidSize <= 0 || bestAskSize <= 0 || bestBidPrice < bestAskPrice);
        assert(abs(inventoryNotional) < maxNotional);
    }

    void orderComplete() {
        state = State::IDLE;
        openOrderId = -1;
        openOrderSide = Side::NUL;
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"

    [[nodiscard]] std::optional<OutboundMsg> decideTrade() {
        assert(maxNotional >= abs(inventoryNotional));
        if (state == State::IDLE) {
            return doTrade();
            assert(state == State::WAITING);
        }
        assert(maxNotional >= abs(inventoryNotional));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<OutboundMsg> doTrade() {// Check for buy signal
        if (theoreticalValue > static_cast<PriceL>(bestAskPrice * (1.0 + thresholdBps))) {
            Qty targetSize = std::min(PriceL(bestAskSize), (maxNotional - inventoryNotional) / bestAskPrice);
            if (targetSize > 0) {
                return submitOrder(Side::BUY, bestAskPrice, targetSize);
            }
        } else if (theoreticalValue < bestBidPrice * (1.0 - thresholdBps)) {
            Qty targetSize = std::min(Qty(bestBidSize), Qty((maxNotional + inventoryNotional) / bestBidPrice));
            if (targetSize > 0) {
                return submitOrder(Side::SELL, bestBidPrice, targetSize);
            }
        }

        return std::nullopt;
    }

};

#endif //HFT_STRATEGY_H
