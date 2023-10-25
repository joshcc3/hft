
#ifndef BACKTEST_L_H
#define BACKTEST_L_H

#include "mytypedefs.h"
#include <iostream>


class BacktestListener {
public:

    virtual void processInbound(TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) = 0;

    virtual void processInbound(TimeNs timeNs, const InboundMsg::OrderModified &update) = 0;

    virtual void processInbound(TimeNs timeNs, const InboundMsg::OrderAccepted &update) = 0;

    virtual void processInbound(TimeNs timeNs, const InboundMsg::OrderCancelled &update) = 0;

    virtual void processInbound(TimeNs timeNs, const InboundMsg::Trade &update) = 0;

    virtual void processOutbound(TimeNs timeNs, const OutboundMsg::Submit &submit) = 0;

    virtual void processOutbound(TimeNs timeNs, const OutboundMsg::Cancel &cancel) = 0;

    virtual void processOutbound(TimeNs timeNs, const OutboundMsg::Modify &modify) = 0;

};

class Logger : public BacktestListener {
public:

    Logger() : BacktestListener() {}

    void processInbound(TimeNs timeNs, const InboundMsg::TopLevelUpdate &update) override {
        char output[100];
        char *fmt = ">,%lld,TOB,%d,%f,%f,%d\n";
        sprintf(output, fmt, timeNs, update.bidSize, getPriceF(update.bidPrice), getPriceF(update.askPrice),
                update.askSize);
        std::cout << output;
    }

    void processInbound(TimeNs timeNs, const InboundMsg::OrderModified &update) override {
        char output[100];
        char *fmt = ">,%lld,M,%d,%d\n";
        sprintf(output, fmt, timeNs, update.id, update.newQty);
        std::cout << output;
    }

    void processInbound(TimeNs timeNs, const InboundMsg::OrderAccepted &update) override {
        char output[100];
        char *fmt = ">,%lld,A,%d\n";
        sprintf(output, fmt, timeNs, update.id);
        std::cout << output;
    }

    void processInbound(TimeNs timeNs, const InboundMsg::OrderCancelled &update) override {
        char output[100];
        char *fmt = ">,%lld,C,%d\n";
        sprintf(output, fmt, timeNs, update.id);
        std::cout << output;
    }

    void processInbound(TimeNs timeNs, const InboundMsg::Trade &update) override {
        char output[100];
        char *fmt = ">,%lld,T,%lld,%f,%d\n";
        sprintf(output, fmt, timeNs, update.id, getPriceF(update.price), update.qty);
        std::cout << output;
    }

    void processOutbound(TimeNs timeNs, const OutboundMsg::Submit &submit) override {
        char output[100];
        char *fmt = "<,%lld,N,%b,%lld,%c,%f,%d\n";
        char s = submit.side == Side::BUY ? 'B' : 'S';
        sprintf(output, fmt, timeNs, submit.isStrategy, submit.orderId, s, getPriceF(submit.orderPrice),
                submit.size);
        std::cout << output;
    }

    void processOutbound(TimeNs timeNs, const OutboundMsg::Cancel &cancel) override {
        char output[100];
        char *fmt = "<,%lld,C,%d\n";
        sprintf(output, fmt, timeNs, cancel.id);
        std::cout << output;
    }

    void processOutbound(TimeNs timeNs, const OutboundMsg::Modify &modify) override {
        char output[100];
        char *fmt = "<,%lld,C,%lld,%d\n";
        sprintf(output, fmt, timeNs, modify.id, modify.size);
        std::cout << output;
    }

};

#endif