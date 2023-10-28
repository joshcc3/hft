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


struct BacktestCfg {
    TimeNs exchangeStratLatency;
    TimeNs stratExchangeLatency;
};

class Backtester {
    constexpr static TimeNs MAX_TIME = (TimeNs(1) << 63);
public:
    Backtester(BacktestCfg cfg, Strategy &s, L3OrderBook<L3Vec> &ob, std::vector<BacktestListener *> &ls) : cfg{cfg},
                                                                                                            strategy(s),
                                                                                                            lob{ob},
                                                                                                            currentTime(
                                                                                                                    0),
                                                                                                            exchangeToStrat{},
                                                                                                            stratToExchange{},
                                                                                                            ls{ls} {}

    void mdEvent(const std::string &line);

private:

    BacktestCfg cfg;
    Strategy &strategy;
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


#endif //HFT_BACKTESTER_H
