#ifndef HFT_BACKTESTER_H
#define HFT_BACKTESTER_H


#include <queue>
#include <cstdint>
#include <variant>
#include <functional>
#include <optional>

#include "RingBuffer.h"
#include "Strategy.h"
#include "L3OrderBook.h"
#include "BacktestListener.cpp"


int
parseLine(const char *msg, TimeNs &timestamp, TimeNs &localTimestamp, bool &isSnapshot, Side &side, PriceL &priceL,
          Qty &qty) {
    int matched = 6;

//        string a = "deribit,BTC-PERPETUAL,";
//        static_assert(sizeof(string) == 24 && a.size() == 22); // small string optimization
    int startIx = 21; // a.size() - 1;

    // 1585699200245000
    int ix = startIx;
    u64 _timestamp = 0;
    for (int i = 0; i < 16; ++i) {
        _timestamp = _timestamp * 10 + (msg[++ix] - '0');
    }
//        assert(_timestamp == timestamp);
    ++ix;
    u64 _localtimestamp = 0;
    for (int i = 0; i < 16; ++i) {
        _localtimestamp = _localtimestamp * 10 + (msg[++ix] - '0');
    }
//        assert(_localtimestamp == localTimestamp);

    ix += 2;
    bool _isSnapshot = msg[ix] == 't';
//        assert(_isSnapshot == isSnapshot);

    ix += _isSnapshot ? 5 : 6;
    Side _side_ = msg[ix] == 'b' ? Side::BUY : Side::SELL;
//        assert(side == _side_);

    PriceL _price{};
    ix += 3;
    u64 decimal = 0;
    while (msg[++ix] != ',' && msg[ix] != '.') {
        decimal = decimal * 10 + (msg[ix] - '0');
    }
    u64 fraction{};
    if (msg[ix] == '.') {
        u64 mult = PRECISION;
        while (msg[++ix] != ',') {
            fraction = fraction * 10 + (msg[ix] - '0');
            mult /= 10;
        }
        _price = decimal * PRECISION + fraction * mult;
    } else {
        _price = decimal * PRECISION;
    }
//        assert(_price == priceL);

    Qty _qty{};
    while (msg[++ix] != '\0') {
        _qty = _qty * 10 + (msg[ix] - '0');
    }
//        assert(_qty == qty);

    timestamp = _timestamp;
    localTimestamp = _localtimestamp;
    isSnapshot = _isSnapshot;
    side = _side_;
    priceL = _price;
    qty = _qty;
    return matched;
}


struct BacktestCfg {
    TimeNs exchangeStratLatency;
    TimeNs stratExchangeLatency;
};

template<typename S>
class Backtester {
    constexpr static TimeNs MAX_TIME = (TimeNs(1) << 63);
public:
    Backtester(BacktestCfg cfg, IStrategy<S> &s, L3OrderBook<L3Vec> &ob, std::vector<BacktestListener *> &ls) : cfg{
            cfg},
                                                                                                                strategy(
                                                                                                                        s),
                                                                                                                lob{ob},
                                                                                                                currentTime(
                                                                                                                        0),
                                                                                                                exchangeToStrat{},
                                                                                                                stratToExchange{},
                                                                                                                ls{ls} {}

    void mdEvent(const std::string &line);

private:

    BacktestCfg cfg;
    IStrategy<S> &strategy;
    L3OrderBook<L3Vec> &lob;
    std::vector<BacktestListener *> ls;

    constexpr static int RING_BUFFER_CAPACITY = 1 << 12;

    RingBuffer<std::pair<TimeNs, InboundMsg>, RING_BUFFER_CAPACITY> exchangeToStrat;
    RingBuffer<std::pair<TimeNs, OutboundMsg>, RING_BUFFER_CAPACITY> stratToExchange;
    TimeNs currentTime;

    template<typename T>
    std::optional<OutboundMsg> processInbound(TimeNs timeNs, const T &update);

    template<typename T>
    std::vector<InboundMsg> processOutbound(TimeNs timeNs, const T &update);

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::TopLevelUpdate &update);

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::OrderModified &update);

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::OrderAccepted &update);

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::OrderCancelled &update);

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::Trade &update);

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::Noop &update);

    [[nodiscard]] std::vector<InboundMsg> _processOutbound(TimeNs timeNs, const OutboundMsg::Submit &submit);

    std::vector<InboundMsg> _processOutbound(TimeNs timeNs, const OutboundMsg::Cancel &cancel);

    std::vector<InboundMsg> _processOutbound(TimeNs timeNs, const OutboundMsg::Modify &modify);
};


template<typename S>
template<typename T>
std::optional<OutboundMsg> Backtester<S>::processInbound(TimeNs timeNs, const T &update) {
    for (auto &l: ls) {
        l->processInbound(timeNs, update);
    }
    return _processInbound(timeNs, update);
}

template<typename S>
template<typename T>
std::vector<InboundMsg> Backtester<S>::processOutbound(TimeNs timeNs, const T &update) {
    for (auto &l: ls) {
        l->processOutbound(timeNs, update);
    }
    return _processOutbound(timeNs, update);
}

template<typename S>
[[nodiscard]] std::optional<OutboundMsg>
Backtester<S>::_processInbound(TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) {

    return strategy.onTopLevelUpdate(timeNs, update);
}

template<typename S>
[[nodiscard]] std::optional<OutboundMsg>
Backtester<S>::_processInbound(TimeNs timeNs, const InboundMsg::OrderModified &update) {
    if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
        return strategy.orderModified(timeNs, update.id, update.newQty);
    } else {
        return std::nullopt;
    }
}

template<typename S>
[[nodiscard]] std::optional<OutboundMsg>
Backtester<S>::_processInbound(TimeNs timeNs, const InboundMsg::OrderAccepted &update) {
    if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
        return strategy.orderAccepted(timeNs, update.id);
    } else {
        return std::nullopt;
    }
}

template<typename S>
[[nodiscard]] std::optional<OutboundMsg>
Backtester<S>::_processInbound(TimeNs timeNs, const InboundMsg::OrderCancelled &update) {
    if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
        return strategy.orderCancelled(timeNs, update.id);
    } else {
        return std::nullopt;
    }
}

template<typename S>
[[nodiscard]] std::optional<OutboundMsg>
Backtester<S>::_processInbound(TimeNs timeNs, const InboundMsg::Noop &update) {
    return std::nullopt;
}

template<typename S>
[[nodiscard]] std::optional<OutboundMsg>
Backtester<S>::_processInbound(TimeNs timeNs, const InboundMsg::Trade &update) {
    return strategy.trade(timeNs, update.id, update.price, update.qty);
}

template<typename S>
[[nodiscard]] std::vector<InboundMsg>
Backtester<S>::_processOutbound(TimeNs timeNs, const OutboundMsg::Submit &submit) {
    const SubmitT output = lob.submit(timeNs, submit.isStrategy, submit.orderId, submit.size,
                                      submit.side,
                                      submit.orderPrice);
    if (output.tag == 0) {
        return {output.res.accept};
    } else if (output.tag == 1) {
        assert(output.size > 1 && output.res.msgs != nullptr &&
               std::holds_alternative<InboundMsg::Trade>(output.res.msgs->content));
        std::vector<InboundMsg> v(output.res.msgs, output.res.msgs + output.size);
        assert(all_of(v.begin(), v.end() - 1,
                      [](const InboundMsg &msg) { return std::holds_alternative<InboundMsg::Trade>(msg.content); })
               && (std::holds_alternative<InboundMsg::OrderAccepted>((v.end() - 1)->content)
                   || std::holds_alternative<InboundMsg::Trade>((v.end() - 1)->content)));
        return v;
    } else {
        assert(false);
    }
}

template<typename S>
std::vector<InboundMsg> Backtester<S>::_processOutbound(TimeNs timeNs, const OutboundMsg::Cancel &cancel) {
    const InboundMsg &output = lob.cancel(timeNs, cancel.id);
    assert(std::holds_alternative<InboundMsg::Noop>(output.content) ||
           std::holds_alternative<InboundMsg::OrderCancelled>(output.content));
    return {output};
}

template<typename S>
std::vector<InboundMsg> Backtester<S>::_processOutbound(TimeNs timeNs, const OutboundMsg::Modify &modify) {
    const InboundMsg output = lob.modify(timeNs, modify.id, modify.price, modify.size);
    assert(std::holds_alternative<InboundMsg::Noop>(output.content) ||
           std::holds_alternative<InboundMsg::OrderModified>(output.content));
    return {output};
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"

template<typename S>
void Backtester<S>::mdEvent(const std::string &line) {

    char _side[10];
    char isSnapshotP[10];
    TimeNs timestamp{};
    TimeNs localTimestamp{};
    float priceF{0};
    int quantity{0};
    PriceL price;
    bool isSnapshot;
    Side side;

    CLOCK(
            int i = 6;
            int matched = parseLine(line.c_str(), timestamp, localTimestamp, isSnapshot, side, price, quantity);
            assert(matched == 6);
            assert(localTimestamp > 0);
            assert(quantity >= 0);
    )


    auto eventNs = localTimestamp * 1000;
    OrderId orderId = l2OrderId(price, side);

    assert(!isSnapshot || quantity > 0);
    assert(price > 0);

    bool outstandingMsgs = true;

    while (outstandingMsgs) {
        bool inMsgFirst;
        bool outMsgFirst;
        TimeNs inTime;
        TimeNs outTime;
        TimeNs initialTime;
        size_t q1;
        size_t q2;
        CLOCK(
                int i = 5;
                initialTime = currentTime;
                q1 = stratToExchange.size();
                q2 = exchangeToStrat.size();

                inTime = !exchangeToStrat.empty() ? exchangeToStrat.front().first : MAX_TIME;
                outTime = !stratToExchange.empty() ? stratToExchange.front().first : MAX_TIME;

                inMsgFirst = inTime <= eventNs && inTime <= outTime;
                outMsgFirst = outTime <= eventNs && outTime < inTime;

                assert(!(inMsgFirst && outMsgFirst));
        )
        if (inMsgFirst) {
            outstandingMsgs = true;
            currentTime = inTime;
            const std::pair<TimeNs, InboundMsg> &in = exchangeToStrat.front();

            CLOCK(
                    int i = 5;
                    std::visit([this](auto &&arg) {
                        const std::optional<OutboundMsg> &cmd = processInbound(currentTime, arg);
                        if (cmd.has_value()) {
                            TimeNs t = currentTime + cfg.stratExchangeLatency;
                            stratToExchange.emplace_back(t, cmd.value());
                        }
                    }, in.second.content);
                    exchangeToStrat.pop();
            )
        } else if (outMsgFirst) {
            outstandingMsgs = true;
            currentTime = outTime;
            const std::pair<TimeNs, OutboundMsg> &out = stratToExchange.front();
            CLOCK(
                    int i = 5;
                    std::visit([this](auto &&arg) {
                        const std::vector<InboundMsg> msgs = processOutbound(currentTime, arg);
                        for (const auto &msg: msgs) {
                            TimeNs t = currentTime + cfg.exchangeStratLatency;
                            exchangeToStrat.emplace_back(t, msg);
                        }
                    }, out.second.content);
                    stratToExchange.pop();
            )
        } else {
            outstandingMsgs = false;
        }

        assert(!outstandingMsgs || q1 - 1 == stratToExchange.size() || q2 - exchangeToStrat.size() <= 1);

        CLOCK(

                int i = 5;
                if (lob.bboUpdated()) {
                    outstandingMsgs = true;
                    exchangeToStrat.emplace_back(currentTime + cfg.exchangeStratLatency, InboundMsg{lob.cached});
                }
        )

        const bool inNotEmpty = !exchangeToStrat.empty();
        const bool outNotEmpty = !stratToExchange.empty();

        assert(currentTime >= initialTime);
        assert(currentTime <= eventNs);
        assert(outstandingMsgs || (!inNotEmpty && !outNotEmpty) ||
               (inNotEmpty && exchangeToStrat.front().first > eventNs) ||
               (outNotEmpty && stratToExchange.front().first > eventNs));

    }

    static bool snapshotting = false;
    CLOCK(
            int i = 4;
            if (isSnapshot) {
                if (!snapshotting) {
                    lob.clear();
                    // TODO - inform all listeners that snapshotting has started
                } else {
                    assert(currentTime == eventNs);
                }
                snapshotting = true;
                currentTime = eventNs;

                SubmitT res = lob.submit(eventNs, false, orderId, quantity, side, price);
                assert(res.tag == 0);
                auto &msg = res.res.accept.content;
                assert(holds_alternative<InboundMsg::OrderAccepted>(msg));
                assert(get<InboundMsg::OrderAccepted>(msg).id == orderId);
                return;
            }
    )
    // TODO - inform all listeners that snapshotting has ended
    // TODO - what happens to outbound messages that happen while snapshotting?
    // TODO - if the strategy attempts to send orders pre-snapshot they cannot be matched against the book.
    // TODO - likewise if the strategy is sending orders
    // TODO - what is the behaviour of our system? The snapped order book could look completely different- so we need to handle
    // informing the orderbook of what to do. the snapshot is not a message that the strategy would receive on the public feed
    // although it would receive it as a disconnect so treat it as you would a disconnect.

    snapshotting = false;


    assert(!isSnapshot && !snapshotting);
    currentTime = eventNs;

    Qty qty = quantity;
    PriceL priceL = price;

    bool newLevel = !lob.orderIdPresent(orderId);

    char msgType = quantity == 0 ? 'D' : (newLevel ? 'A' : 'U');

    switch (msgType) {
        case 'A': {
            std::vector<InboundMsg> msgs;
            CLOCK(
                    int i = 1;
                    msgs = move(processOutbound(eventNs,
                                                OutboundMsg::Submit(false,
                                                                    orderId, side, priceL,
                                                                    qty)));
            )
            for (const auto &msg: msgs) {
                exchangeToStrat.emplace_back(currentTime + cfg.exchangeStratLatency, msg);
            }
            break;
        }
        case 'D': {
            assert(qty == 0);
            CLOCK(
                    int i = 2;
                    processOutbound(eventNs, OutboundMsg::Cancel(orderId));
            )
            break;
        }
        case 'U': {
            CLOCK(
                    int i = 3;
                    processOutbound(eventNs, OutboundMsg::Modify(orderId, priceL, qty));
            )
            break;
        }
        default: {
            std::cerr << "Unexpected type [" << msgType << "]." << std::endl;
            exit(1);
        }

    }

    CLOCK(
            int i = 5;

            if (lob.bboUpdated()) {
                exchangeToStrat.emplace_back(currentTime + cfg.exchangeStratLatency, InboundMsg{lob.cached});
            }
    )

    assert(lob.lastUpdateTs == currentTime);
    assert(currentTime == eventNs);
    assert(exchangeToStrat.empty() || exchangeToStrat.front().first > currentTime);
    assert(stratToExchange.empty() || stratToExchange.front().first > currentTime);

}

#pragma clang diagnostic pop

#endif //HFT_BACKTESTER_H

