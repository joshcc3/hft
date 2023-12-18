Backtester
===========

The solution is written in C++17. It uses cmake. For the python file only pandas needs to be installed.

The project contains the following components:

A Backtester, L3OrderBook, sample strategy and an analysis tool.

The backtester processes market-data events and maintains 2 ring buffers from and to the strategy 
containing messages in transit.
It moves forward in time every marketdata event and assumes an end of day series of messages cancelling all
open orders. It processes the events in a time ordered fashion enqueuing new events the strategy might
have or tob updates from the matching engine. All queue events are logged out (Logger), sample input/output
is in the data directory.

L3OrderBook implements a matching engine and the orderbook state. It supports submit, modify cancels according 
to the specification (price/time ordered, reduces preserve queue position). 
Its implemented using 2 lists for bid/ask and the order list is a 
vector<pair<OrderId, Order>> (based on benchmarks performed superior to unordered_map for insertions/deletions/lookups upto 75 elements).
It assumes a seperate OrderId space for strategy orders to prevent conflicts. It emits as few acknowledgements as possible
(an aggressive trade would only emit Trade messages unless there is a resting order in which case there would be OrderAccepted).

The sample strategy is simple: it maintains an ewma of the vwap of the bbo and submits an aggressive limit
when the bbo crosses with this value by a user defined threshold. It maintains a single order in the market cancelling 
that order unless it gets filled immediately.

There is a data directory containing sample input and output files from running the backtester. 
The python script needs to be updated with the filename of the output file to run it.


Limitations:
Apart from general considerations attention has not explicitly been paid to performance or scaling this
to run in parallel.
The strategy does have an affect on order liquidity and the backtester makes no attempt to reconcile it
with the marketdata.

The interface provided to the strategy is relatively primitive but could be extended easily.
Real hft strategies would need to consider their execution far more carefully as well as manage
stacks of orders. They would need to also obey exchange limitations and threshold such as risk
checks, message limits, invalid orders.
They would also need to handle exchange timeouts and cancel on disconnect logic.
The pnl also consists of a large number of costs, like trading fees, borrow costs, marketdata costs,
If the connection falls too far behind the exchange would usually terminate the connection.
We would need to handle reconnection to exchanges as well.


"deribit,BTC-PERPETUAL,1585734999117000,1585734999132100,false,bid,6295.5,155610"