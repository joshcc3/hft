import pandas as pd

def m2mPnl(fname):
    with open(fname) as f:
        logLines = f.read().splitlines()
    parsedLines = [x.split(",") for x in logLines if x[0] == '<' or x[0] == '>']
    orders = {t[4]: t for t in parsedLines if t[2] == "N"}
    trades = pd.DataFrame([t for t in parsedLines if t[2] == "T"], columns=["dir", "time", "msg", "id", "price", "qty"]).astype({"id": "int", "price": "float", "qty": "float"})
    tob = pd.DataFrame([t for t in parsedLines if t[2] == "TOB"], columns=["dir", "time", "msg", "bidQty", "bidPrice", "askQty", "askPrice"]).astype({"bidQty": "float", "askQty": "float", "bidPrice": "float", "askPrice": "float"})
    [_d, _t, _m, _q, bidPrice, _1, _2] = tob[(tob.bidQty > 0)].values[-1]
    [_d, _t, _m, _1, _2, askPrice, _q] = tob[(tob.askQty > 0)].values[-1]
    trades['isStrat'] = trades.apply(lambda x: orders[str(x.id)][3] == '1', axis=1)
    trades['side'] = trades.apply(lambda x: orders[str(x.id)][5], axis=1)
    trades['m2mPrice'] = trades['side'].apply(lambda x: float(bidPrice) if x == 'B' else float(askPrice))
    trades['direction'] = trades['side'].apply(lambda x: 1 if x == 'B' else -1)
    trades['m2mPnl'] = ((trades.m2mPrice - trades.price) * trades.qty * trades.direction)
    stratTrades = trades[trades['isStrat']]
    totPnl = stratTrades['m2mPnl'].sum()

    print("Final Bid/Ask [{}/{}]".format(bidPrice, askPrice))
    print("Total PnL: {}".format(totPnl))
    print()
    print("Strategy Trades")
    print("---------------")
    print(stratTrades[['time', 'm2mPnl', 'side', 'price', 'qty']])


if __name__ == '__main__':
    print("---------------- Latency 1ns 1ns ------------------------")
    print()
    m2mPnl("./data/latencyRaceOutput1_1.csv")
    print()
    print()
    print("---------------- Latency 1ns 20ns ------------------------")
    print()
    m2mPnl("./data/latencyRaceOutput1_20.csv")
