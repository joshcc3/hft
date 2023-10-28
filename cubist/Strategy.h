#ifndef HFT_STRATEGY_H
#define HFT_STRATEGY_H

#include <iostream>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <optional>
#include <functional>
#include <utility>

#include "mytypedefs.h"

template<typename T>
class IStrategy {

public:

    // public messages
    std::optional<OutboundMsg>
    onTopLevelUpdate([[maybe_unused]] TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) {
        return static_cast<T *>(this)->_onTopLevelUpdate(timeNs, update);
    }

    std::optional<OutboundMsg> orderModified([[maybe_unused]] TimeNs time, [[maybe_unused]] OrderId id,
                                             [[maybe_unused]] Qty newQty) {
        return static_cast<T *>(this)->_orderModified(time, id, newQty);
    }

    [[nodiscard]] std::optional<OutboundMsg> orderAccepted([[maybe_unused]] TimeNs time, OrderId id) {
        return static_cast<T *>(this)->_orderAccepted(time, id);
    }

    std::optional<OutboundMsg> orderCancelled([[maybe_unused]] TimeNs time, OrderId id) {
        return static_cast<T *>(this)->_orderCancelled(time, id);
    }

    std::optional<OutboundMsg> trade([[maybe_unused]] TimeNs time, OrderId id, PriceL price, Qty qty) {
        return static_cast<T *>(this)->_trade(time, id, price, qty);
    }

    [[nodiscard]] std::optional<OutboundMsg> submitOrder(Side side, PriceL price, Qty size) {
        return static_cast<T *>(this)->_submitOrder(side, price, size);
    }

    [[nodiscard]] std::optional<OutboundMsg> submitCancel(OrderId orderId) {
        return static_cast<T *>(this)->_submitCancel(orderId);
    }

private:

};

class DummyStrategy : public IStrategy<DummyStrategy> {
public:
    std::optional<OutboundMsg>
    _onTopLevelUpdate([[maybe_unused]] TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) { return {}; }

    std::optional<OutboundMsg> _orderModified([[maybe_unused]] TimeNs time, [[maybe_unused]] OrderId id,
                                             [[maybe_unused]] Qty newQty) { return {}; }

    [[nodiscard]] std::optional<OutboundMsg> _orderAccepted([[maybe_unused]] TimeNs time, OrderId id) { return {}; }

    std::optional<OutboundMsg> _orderCancelled([[maybe_unused]] TimeNs time, OrderId id) { return {}; }

    std::optional<OutboundMsg> _trade([[maybe_unused]] TimeNs time, OrderId id, PriceL price, Qty qty) { return {}; }

    [[nodiscard]] std::optional<OutboundMsg> _submitOrder(Side side, PriceL price, Qty size) { return {}; }

    [[nodiscard]] std::optional<OutboundMsg> _submitCancel(OrderId orderId) { return {}; }
};

class Strategy : public IStrategy<Strategy> {

    const double alpha;
    const double thresholdBps;
    const PriceL maxNotional; // Max inventory notional


    std::function<OrderId()> nextOrderId;

    enum class State {
        IDLE,
        WAITING_ACCEPT,
        WAITING_CANCEL
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

    std::optional<OutboundMsg>
    _onTopLevelUpdate([[maybe_unused]] TimeNs timeNs, const InboundMsg::TopLevelUpdate &update);

    std::optional<OutboundMsg> _orderModified([[maybe_unused]] TimeNs time, [[maybe_unused]] OrderId id,
                                             [[maybe_unused]] Qty newQty);

    [[nodiscard]] std::optional<OutboundMsg> _orderAccepted([[maybe_unused]] TimeNs time, OrderId id);

    std::optional<OutboundMsg> _orderCancelled([[maybe_unused]] TimeNs time, OrderId id);

    std::optional<OutboundMsg> _trade([[maybe_unused]] TimeNs time, OrderId id, PriceL price, Qty qty);

    [[nodiscard]] std::optional<OutboundMsg> _submitOrder(Side side, PriceL price, Qty size);

    [[nodiscard]] std::optional<OutboundMsg> _submitCancel(OrderId orderId);

    // public messages

private:

    void stateChecks();

    void orderComplete();

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"

    [[nodiscard]] std::optional<OutboundMsg> decideTrade();

    [[nodiscard]] std::optional<OutboundMsg> doTrade();

};

#endif //HFT_STRATEGY_H
