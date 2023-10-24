//
// Created by jc on 24/10/23.
//

#include "L3OrderBook.h"
#include

bool L3OrderBook::empty() {

}

InboundMsg::TopLevelUpdate L3OrderBook::getBBO() {

}

std::optional<InboundMsg::Trade>
L3OrderBook::submit(bool isStrategy, OrderId orderId, Qty size, Side side, PriceL orderPrice) {
}

void L3OrderBook::cancel(OrderId id) {

}

void L3OrderBook::modify(OrderId orderId, Qty size) {

}
