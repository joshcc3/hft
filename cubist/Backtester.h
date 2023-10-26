#ifndef HFT_BACKTESTER_H
#define HFT_BACKTESTER_H


#include <queue>
#include <cstdint>
#include <variant>
#include <functional>
#include <optional>

#include "ring_buffer.h"
#include "Strategy.h"
#include "L3OrderBook.h"
#include "BacktestListener.cpp"


// TODO - mark all functions noexcept
// TODO - how do we deal with the fact that in our backtester there might be modifies or deletes
// in the marketdata for orders that have been matched and therefore don't exist in the marketdata?
// Do we simply ignore those updates for order ids that don't exist accepting that inaccuracy?

struct BacktestCfg {
    TimeNs exchangeStratLatency;
    TimeNs stratExchangeLatency;
};

class Backtester {
    constexpr static TimeNs MAX_TIME = (TimeNs(1) << 63);
public:
    Backtester(BacktestCfg cfg, Strategy &s, L3OrderBook<L3Vec> &ob, vector<BacktestListener *> &ls) : cfg{cfg},
                                                                                                       strategy(s),
                                                                                                       lob{ob},
                                                                                                       currentTime(0),
                                                                                                       exchangeToStrat{},
                                                                                                       stratToExchange{},
                                                                                                       ls{ls} {}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"

    void mdEvent(const std::string &line) {
        // 1. Parse the incoming market data event

        TimeNs eventNs{0};
        char _msgType[20] = {};
        OrderId id{0};
        int matched = sscanf(line.c_str(), "%ld,%s%ld", &eventNs, _msgType, &id); // NOLINT(*-err34-c)
        assert(matched == 3 && id > 0 && eventNs > 0);

        // TODO update current time
        bool outstandingMsgs = true;
        // 2. Process the messages in the vectors up to the timestamp of the current event
        while (outstandingMsgs) {
            TimeNs initialTime = currentTime;
            size_t q1 = stratToExchange.size();
            size_t q2 = exchangeToStrat.size();

            TimeNs inTime = !exchangeToStrat.empty() ? exchangeToStrat.front().first : MAX_TIME;
            TimeNs outTime = !stratToExchange.empty() ? stratToExchange.front().first : MAX_TIME;

            bool inMsgFirst = inTime <= eventNs && inTime <= outTime;
            bool outMsgFirst = outTime <= eventNs && outTime < inTime;

            assert(!(inMsgFirst && outMsgFirst));

            if (inMsgFirst) {
                outstandingMsgs = true;
                currentTime = inTime;
                const std::pair<TimeNs, InboundMsg> &in = exchangeToStrat.front();

                std::visit([this](auto &&arg) {
                    const std::optional<OutboundMsg> &cmd = processInbound(currentTime, arg);
                    if (cmd.has_value()) {
                        TimeNs t = currentTime + cfg.stratExchangeLatency;
                        stratToExchange.emplace_back(t, cmd.value());
                    }
                }, in.second.content);
                exchangeToStrat.pop();
            } else if (outMsgFirst) {
                outstandingMsgs = true;
                currentTime = outTime;
                const std::pair<TimeNs, OutboundMsg> &out = stratToExchange.front();
                std::visit([this](auto &&arg) {
                    const std::vector<InboundMsg> msgs = processOutbound(currentTime, arg);
                    for (const auto &msg: msgs) {
                        TimeNs t = currentTime + cfg.exchangeStratLatency;
                        exchangeToStrat.emplace_back(t, msg);
                    }
                }, out.second.content);
                stratToExchange.pop();
            } else {
                outstandingMsgs = false;
            }

            bool inNotEmpty = !exchangeToStrat.empty();
            bool outNotEmpty = !stratToExchange.empty();

            assert(currentTime >= initialTime);
            assert(currentTime <= eventNs);
            assert(outstandingMsgs || (!inNotEmpty && !outNotEmpty) ||
                   (inNotEmpty && exchangeToStrat.front().first > eventNs) ||
                   (outNotEmpty && stratToExchange.front().first > eventNs));
            assert(!outstandingMsgs || q1 - 1 == stratToExchange.size() || q2 - 1 == exchangeToStrat.size());

        }

        currentTime = eventNs;

        switch (_msgType[0]) {
            case 'A': {
                OrderId orderId{0};
                char _side{'-'};
                Qty qty{-1};
                float priceF{0};
                int matchedA = sscanf(line.c_str(), "%ld,%s%ld, %c,%d,%f", &eventNs, _msgType, &orderId, &_side,
                                      &qty, // NOLINT(*-err34-c)
                                      &priceF);
                assert(matchedA == 6 && orderId > 0 && (_side == 'B' || _side == 'S') && priceF > 0 && qty > 0);
                Side side = _side == 'B' ? Side::BUY : Side::SELL;
                PriceL priceL{PriceL(priceF * double(PRECISION))};
                const std::vector<InboundMsg> msgs = processOutbound(eventNs,
                                                                     OutboundMsg::Submit(false,
                                                                                         orderId, side, priceL,
                                                                                         qty));

                for (const auto &msg: msgs) {
                    exchangeToStrat.emplace_back(currentTime + cfg.exchangeStratLatency, msg);
                }
                break;
            }
            case 'D': {
                // TODO - handle all updates in strategy.
                processOutbound(eventNs, OutboundMsg::Cancel(id));
                break;
            }
            case 'U': {
                char _side{'-'};
                Qty qty{-1};
                int matchedUpdate = sscanf(line.c_str(), "%ld,%s%ld, %c,%d", &eventNs, _msgType, &id, &_side,
                                           &qty); // NOLINT(*-err34-c)
                assert(matchedUpdate == 5);
                processOutbound(eventNs, OutboundMsg::Modify(id, qty));
                break;
            }
            default: {
                std::cerr << "Unexpected type [" << _msgType << "]." << std::endl;

            }

        }

        if (lob.bboUpdated()) {
            exchangeToStrat.emplace_back(currentTime + cfg.exchangeStratLatency, InboundMsg{lob.cached});
        }

        assert(lob.lastUpdateTs == currentTime);
        assert(currentTime == eventNs);
        assert(exchangeToStrat.empty() || exchangeToStrat.front().first > currentTime);
        assert(stratToExchange.empty() || stratToExchange.front().first > currentTime);

    }

#pragma clang diagnostic pop

private:

    BacktestCfg cfg;
    Strategy &strategy;
    L3OrderBook<L3Vec> &lob;
    vector<BacktestListener *> ls;

    constexpr static int RING_BUFFER_CAPACITY = 1 << 12;

    ring_buffer<std::pair<TimeNs, InboundMsg>, RING_BUFFER_CAPACITY> exchangeToStrat;
    ring_buffer<std::pair<TimeNs, OutboundMsg>, RING_BUFFER_CAPACITY> stratToExchange;
    TimeNs currentTime;

    // Processing function for each outbound type

    template<typename T>
    [[nodiscard]] std::optional<OutboundMsg> processInbound(TimeNs timeNs, const T &update) {
        for (auto &l: ls) {
            l->processInbound(timeNs, update);
        }
        return _processInbound(timeNs, update);
    }

    template<typename T>
    [[nodiscard]] std::vector<InboundMsg> processOutbound(TimeNs timeNs, const T &update) {
        for (auto &l: ls) {
            l->processOutbound(timeNs, update);
        }
        return _processOutbound(timeNs, update);
    }

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) {

        return strategy.onTopLevelUpdate(timeNs, update);
    }

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::OrderModified &update) {
        if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
            return strategy.orderModified(timeNs, update.id, update.newQty);
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::OrderAccepted &update) {
        if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
            return strategy.orderAccepted(timeNs, update.id);
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::OrderCancelled &update) {
        if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
            return strategy.orderCancelled(timeNs, update.id);
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<OutboundMsg> _processInbound(TimeNs timeNs, const InboundMsg::Trade &update) {
        return strategy.trade(timeNs, update.id, update.price, update.qty);
    }

    [[nodiscard]] std::vector<InboundMsg> _processOutbound(TimeNs timeNs, const OutboundMsg::Submit &submit) {
        const vector<InboundMsg> &output =
                lob.submit(timeNs, submit.isStrategy, submit.orderId, submit.size,
                           submit.side, submit.orderPrice);
        assert(all_of(output.begin(), output.end() - 1,
                      [](const InboundMsg &msg) { return holds_alternative<InboundMsg::Trade>(msg.content); })
               && (holds_alternative<InboundMsg::OrderAccepted>((output.end() - 1)->content)
                   || holds_alternative<InboundMsg::Trade>((output.end() - 1)->content)));
        return output;
    }

    std::vector<InboundMsg> _processOutbound(TimeNs timeNs, const OutboundMsg::Cancel &cancel) {
        const vector<InboundMsg> &output = lob.cancel(timeNs, cancel.id);
        assert(output.empty() ||
               output.size() == 1 && holds_alternative<InboundMsg::OrderCancelled>(output[0].content));
        return output;
    }

    std::vector<InboundMsg> _processOutbound(TimeNs timeNs, const OutboundMsg::Modify &modify) {
        const vector<InboundMsg> &output = lob.modify(timeNs, modify.id, modify.size);
        assert(all_of(output.begin(), output.end() - 1,
                      [](const InboundMsg &msg) { return holds_alternative<InboundMsg::Trade>(msg.content); })
               && (holds_alternative<InboundMsg::OrderModified>((output.end() - 1)->content)
                   || holds_alternative<InboundMsg::Trade>((output.end() - 1)->content)));
        return output;
    }

};


#endif //HFT_BACKTESTER_H
