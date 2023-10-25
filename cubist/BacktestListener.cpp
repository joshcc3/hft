
#ifndef BACKTEST_L_H
#define BACKTEST_L_H

#include "mytypedefs.h"
#include <iostream>


class BacktestListener {
public:

    virtual void processInbound(const InboundMsg::TopLevelUpdate &update) = 0;

    virtual void processInbound(const InboundMsg::OrderModified &update) = 0;

    virtual void processInbound(const InboundMsg::OrderAccepted &update) = 0;

    virtual void processInbound(const InboundMsg::OrderCancelled &update) = 0;

    virtual void processInbound(const InboundMsg::Trade &update) = 0;

    virtual void processOutbound(const OutboundMsg::Submit &submit) = 0;

    virtual void processOutbound(const OutboundMsg::Cancel &cancel) = 0;

    virtual void processOutbound(const OutboundMsg::Modify &modify) = 0;

};

class Logger : public BacktestListener {
public:

    Logger() : BacktestListener() {}

    void processInbound(const InboundMsg::TopLevelUpdate &update) override {
        char output[100];
        char *fmt = ">,%d,TOB,%d,%f,%f,%d\n";
        sprintf(output, fmt, update.time, update.bidSize, getPriceF(update.bidPrice), getPriceF(update.askPrice),
                update.askSize);
        std::cout << output;
    }

    void processInbound(const InboundMsg::OrderModified &update) override {
        char output[100];
        char *fmt = ">,%d,M,%d,%d\n";
        sprintf(output, fmt, update.timeNs, update.id, update.newQty);
        std::cout << output;
    }

    void processInbound(const InboundMsg::OrderAccepted &update) override {
        char output[100];
        char *fmt = ">,%d,A,%d\n";
        sprintf(output, fmt, update.timeNs, update.id);
        std::cout << output;
    }

    void processInbound(const InboundMsg::OrderCancelled &update) override {
        char output[100];
        char *fmt = ">,%d,C,%d\n";
        sprintf(output, fmt, update.timeNs, update.id);
        std::cout << output;
    }

    void processInbound(const InboundMsg::Trade &update) override {
        char output[100];
        char *fmt = ">,%d,T,%d,%f,%d\n";
        sprintf(output, fmt, update.timeNs, update.id, getPriceF(update.price), update.qty);
        std::cout << output;
    }

    void processOutbound(const OutboundMsg::Submit &submit) override {
        char output[100];
        char *fmt = "<,%d,N,%b,%d,%c,%f,%d\n";
        char s = submit.side == Side::BUY ? 'B' : 'S';
        sprintf(output, fmt, submit.timeNs, submit.isStrategy, submit.orderId, s, getPriceF(submit.orderPrice),
                submit.size);
        std::cout << output;
    }

    void processOutbound(const OutboundMsg::Cancel &cancel) override {
        char output[100];
        char *fmt = "<,%d,C,%d\n";
        sprintf(output, fmt, cancel.timeNs, cancel.id);
        std::cout << output;
    }

    void processOutbound(const OutboundMsg::Modify &modify) override {
        char output[100];
        char *fmt = "<,%d,C,%d,%d\n";
        sprintf(output, fmt, modify.timeNs, modify.id, modify.size);
        std::cout << output;
    }

};

#endif