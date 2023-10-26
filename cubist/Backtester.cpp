#include "Backtester.h"
#include <functional>
#include <iostream>
#include <fstream>
#include <string>


template<typename T>
std::optional<OutboundMsg> Backtester::processInbound(TimeNs timeNs, const T &update) {
    for (auto &l: ls) {
        l->processInbound(timeNs, update);
    }
    return _processInbound(timeNs, update);
}

template<typename T>
std::vector<InboundMsg> Backtester::processOutbound(TimeNs timeNs, const T &update) {
    for (auto &l: ls) {
        l->processOutbound(timeNs, update);
    }
    return _processOutbound(timeNs, update);
}

[[nodiscard]] std::optional<OutboundMsg> Backtester::_processInbound(TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) {

    return strategy.onTopLevelUpdate(timeNs, update);
}

[[nodiscard]] std::optional<OutboundMsg> Backtester::_processInbound(TimeNs timeNs, const InboundMsg::OrderModified &update) {
    if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
        return strategy.orderModified(timeNs, update.id, update.newQty);
    } else {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<OutboundMsg> Backtester::_processInbound(TimeNs timeNs, const InboundMsg::OrderAccepted &update) {
    if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
        return strategy.orderAccepted(timeNs, update.id);
    } else {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<OutboundMsg> Backtester::_processInbound(TimeNs timeNs, const InboundMsg::OrderCancelled &update) {
    if (L3OrderBook<L3Vec>::isStrategyOrder(update.id)) {
        return strategy.orderCancelled(timeNs, update.id);
    } else {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<OutboundMsg> Backtester::_processInbound(TimeNs timeNs, const InboundMsg::Trade &update) {
    return strategy.trade(timeNs, update.id, update.price, update.qty);
}

[[nodiscard]] std::vector<InboundMsg> Backtester::_processOutbound(TimeNs timeNs, const OutboundMsg::Submit &submit) {
    const std::vector<InboundMsg> &output =
            lob.submit(timeNs, submit.isStrategy, submit.orderId, submit.size,
                       submit.side, submit.orderPrice);
    assert(all_of(output.begin(), output.end() - 1,
                  [](const InboundMsg &msg) { return std::holds_alternative<InboundMsg::Trade>(msg.content); })
           && (std::holds_alternative<InboundMsg::OrderAccepted>((output.end() - 1)->content)
               || std::holds_alternative<InboundMsg::Trade>((output.end() - 1)->content)));
    return output;
}

std::vector<InboundMsg> Backtester::_processOutbound(TimeNs timeNs, const OutboundMsg::Cancel &cancel) {
    const std::vector<InboundMsg> &output = lob.cancel(timeNs, cancel.id);
    assert(output.empty() ||
           output.size() == 1 && std::holds_alternative<InboundMsg::OrderCancelled>(output[0].content));
    return output;
}

std::vector<InboundMsg> Backtester::_processOutbound(TimeNs timeNs, const OutboundMsg::Modify &modify) {
    const std::vector<InboundMsg> &output = lob.modify(timeNs, modify.id, modify.size);
    assert(all_of(output.begin(), output.end() - 1,
                  [](const InboundMsg &msg) { return std::holds_alternative<InboundMsg::Trade>(msg.content); })
           && (std::holds_alternative<InboundMsg::OrderModified>((output.end() - 1)->content)
               || std::holds_alternative<InboundMsg::Trade>((output.end() - 1)->content)));
    return output;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-result"

void Backtester::mdEvent(const std::string &line) {
    // 1. Parse the incoming market data event

    TimeNs eventNs{0};
    char _msgType[20] = {};
    OrderId id{0};
    int matched = sscanf(line.c_str(), "%ld,%s%ld", &eventNs, _msgType, &id); // NOLINT(*-err34-c)
    assert(matched == 3 && id > 0 && eventNs > 0);

    // TODO update current time
    bool outstandingMsgs = true;
    // 2. Process the messages in the std::vectors up to the timestamp of the current event
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

        assert(!outstandingMsgs || q1 - 1 == stratToExchange.size() || q2 - exchangeToStrat.size() <= 1);

        if (lob.bboUpdated()) {
            outstandingMsgs = true;
            exchangeToStrat.emplace_back(currentTime + cfg.exchangeStratLatency, InboundMsg{lob.cached});
        }

        bool inNotEmpty = !exchangeToStrat.empty();
        bool outNotEmpty = !stratToExchange.empty();

        assert(currentTime >= initialTime);
        assert(currentTime <= eventNs);
        assert(outstandingMsgs || (!inNotEmpty && !outNotEmpty) ||
               (inNotEmpty && exchangeToStrat.front().first > eventNs) ||
               (outNotEmpty && stratToExchange.front().first > eventNs));

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

int main(int argc, char *argv[]) {
    if(argc != 4) {
        std::cerr << "Usage: [./backtester [exchange-strat-latency] [strategy-exchange-latency] [input-file]]" << std::endl;
        exit(1);
    }

    TimeNs exchangeStratLatency = atoi(argv[1]);
    TimeNs stratExchangeLatency = atoi(argv[2]);
    std::string inputFile = std::string(argv[3]);

    if(exchangeStratLatency <= 0 || stratExchangeLatency <= 0) {
        std::cerr << "Latencies must be > 0." << std::endl;
    }

    std::ifstream ifile(inputFile);
    if (!ifile) {
        std::cerr << "Failed to open the file." << std::endl;
        return 1;
    }

    BacktestCfg cfg{exchangeStratLatency, stratExchangeLatency};

    L3OrderBook<L3Vec> lob;
    std::function<OrderId()> nextOrderId = [lob]() mutable { return lob.nextOrderId(); };
    Strategy strategy{0.1, 0.005, 100'000, nextOrderId};

    Logger l;
    std::vector<BacktestListener*> ls{&l};

    Backtester b{cfg, strategy, lob, ls};

    std::string line;
    while (std::getline(ifile, line)) {
        b.mdEvent(line);
    }

    std::cout << std::endl;

    return 0;

}
