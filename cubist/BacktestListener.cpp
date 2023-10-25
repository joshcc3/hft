//
// Created by jc on 24/10/23.
//

#include "mytypedefs.h"
#include <iostream>

template<typename Derived>
class BacktestListenerCRTP {
public:

    template<typename T>
    void processInbound(const T &update) {
        static_cast<Derived>(this)->processInbound(update);
    }

    template<typename T>
    void processOutbound(const T &msg) {
        static_cast<Derived>(this)->processOutbound(msg);
    }

};

class BacktestListener {
public:

    virtual void processInbound(const InboundMsg::TopLevelUpdate &update);

    virtual void processInbound(const InboundMsg::OrderModified &update);

    virtual void processInbound(const InboundMsg::OrderAccepted &update);

    virtual void processInbound(const InboundMsg::OrderCancelled &update);

    virtual void processInbound(const InboundMsg::Trade &update);

    virtual void processOutbound(const OutboundMsg::Submit &submit);

    virtual void processOutbound(const OutboundMsg::Cancel &cancel);

    virtual void processOutbound(const OutboundMsg::Modify &modify);

};

class Logger : BacktestListener {

    void processInbound(const InboundMsg::TopLevelUpdate &update) override {
        char output[100];
        char *fmt = ">,TOB,%d,%f,%f,%d\n";
        sprintf(output, fmt, update.bidSize, getPriceF(update.bidPrice), getPriceF(update.askPrice), update.askSize);
        std::cout << output;
    }

    void processInbound(const InboundMsg::OrderModified &update) override {
        char output[100];
        char *fmt = ">,M,%d,%d\n";
        sprintf(output, fmt, update.id, update.newQty);
        std::cout << output;
    }

    void processInbound(const InboundMsg::OrderAccepted &update) override {
        char output[100];
        char *fmt = ">,A,%d\n";
        sprintf(output, fmt, update.id);
        std::cout << output;
    }

    void processInbound(const InboundMsg::OrderCancelled &update) override {
        char output[100];
        char *fmt = ">,C,%d\n";
        sprintf(output, fmt, update.id);
        std::cout << output;
    }

    void processInbound(const InboundMsg::Trade &update) override {
        char output[100];
        char *fmt = ">,T,%d,%f,%d\n";
        sprintf(output, fmt, update.id, getPriceF(update.price), update.qty);
        std::cout << output;
    }

    void processOutbound(const OutboundMsg::Submit &submit) override {
        char output[100];
        char *fmt = "<,S,%b,%d,%c,%f,%d\n";
        sprintf(output, fmt, submit.isStrategy, submit.orderId, submit.side, getPriceF(submit.orderPrice), submit.size);
        std::cout << output;
    }

    void processOutbound(const OutboundMsg::Cancel &cancel) override {
        char output[100];
        char *fmt = "<,C,%d\n";
        sprintf(output, fmt, cancel.id);
        std::cout << output;
    }

    void processOutbound(const OutboundMsg::Modify &modify) override {
        char output[100];
        char *fmt = "<,C,%d,%d\n";
        sprintf(output, fmt, modify.id, modify.size);
        std::cout << output;
    }


};