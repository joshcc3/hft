#include "Strategy.h"

std::optional<OutboundMsg>
Strategy::onTopLevelUpdate([[maybe_unused]] TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) {
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

        PriceL vwap = (bestBidPrice * bestAskSize + bestAskPrice * bestBidSize) / (bestBidSize + bestAskSize);

        theoreticalValue =
                theoreticalValue > 0 ? static_cast<PriceL>(alpha * double(vwap) +
                                                           (1.0 - alpha) * static_cast<double>(theoreticalValue))
                                     : vwap;


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

std::optional<OutboundMsg> Strategy::orderModified([[maybe_unused]] TimeNs time, [[maybe_unused]] OrderId id,
                                                   [[maybe_unused]] Qty newQty) {
    return std::nullopt;
}

[[nodiscard]] std::optional<OutboundMsg> Strategy::orderAccepted([[maybe_unused]] TimeNs time, OrderId id) {
    stateChecks();
    assert(id == openOrderId);
    assert(state == State::WAITING_ACCEPT);
    assert(openOrderId == id);

    return submitCancel(id);
}

std::optional<OutboundMsg> Strategy::orderCancelled([[maybe_unused]] TimeNs time, OrderId id) {
    stateChecks();
    assert(id == openOrderId);
    assert(state == State::WAITING_CANCEL);
    assert(openOrderId == id);

    orderComplete();
    return std::nullopt;
}

std::optional<OutboundMsg> Strategy::trade([[maybe_unused]] TimeNs time, OrderId id, PriceL price, Qty qty) {
    stateChecks();
    assert(price <= 1e6 * PRECISION);
    assert(qty <= 1e6 * PRECISION);

    if (id == openOrderId) {
        assert(state == State::WAITING_ACCEPT || state == State::WAITING_CANCEL);
        if (openOrderSide == Side::BUY) {
            inventoryNotional += NotionalL(price * qty);
        } else {
            inventoryNotional -= NotionalL(price * qty);
        }

        openOrderQty -= qty;

        if (openOrderQty == 0) {
            orderComplete();
            stateChecks();
            return std::nullopt;
        } else if (state == State::WAITING_ACCEPT) {
            const std::optional<OutboundMsg> &msg = submitCancel(id);
            stateChecks();
            return msg;
        }

    }
    return std::nullopt;
}

[[nodiscard]] std::optional<OutboundMsg> Strategy::submitOrder(Side side, PriceL price, Qty size) {
    std::cout << "Order Submitted: " << (side == Side::BUY ? "BUY" : "SELL") << " " << size << " @ "
              << static_cast<double>(price) * 1e-9 << "(Theo: " << theoreticalValue / double(PRECISION) << ")"
              << std::endl;

    assert(state == State::IDLE);
    assert((maxNotional - inventoryNotional) / size >= price);
    assert(side == Side::BUY && price >= bestAskPrice || side == Side::SELL && price <= bestBidPrice);

    state = State::WAITING_ACCEPT;
    openOrderId = nextOrderId();
    openOrderSide = side;
    openOrderQty = size;

    stateChecks();

    return std::make_optional<>(OutboundMsg{OutboundMsg::Submit{true, openOrderId, side, price, size}});
}

[[nodiscard]] std::optional<OutboundMsg> Strategy::submitCancel(OrderId orderId) {
    assert(state == State::WAITING_ACCEPT);
    assert(openOrderId == orderId);

    state = State::WAITING_CANCEL;
    std::cout << "Order Cancel: " << orderId << std::endl;
    return std::make_optional<>(OutboundMsg{OutboundMsg::Cancel{orderId}});
}

void Strategy::stateChecks() {
    assert((openOrderId == -1) == (state == State::IDLE) == (openOrderSide == Side::NUL) == (openOrderQty == -1));
    assert(bestBidSize <= 0 || bestAskSize <= 0 || bestBidPrice < bestAskPrice);
    assert(abs(inventoryNotional) < maxNotional);
}

void Strategy::orderComplete() {
    state = State::IDLE;
    openOrderId = -1;
    openOrderSide = Side::NUL;
    openOrderQty = -1;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"

[[nodiscard]] std::optional<OutboundMsg> Strategy::decideTrade() {
    assert(maxNotional >= abs(inventoryNotional));
    if (state == State::IDLE) {
        return doTrade();
        assert(state == State::WAITING_ACCEPT);
    }
    assert(maxNotional >= abs(inventoryNotional));
    return std::nullopt;
}

[[nodiscard]] std::optional<OutboundMsg> Strategy::doTrade() {// Check for buy signal
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