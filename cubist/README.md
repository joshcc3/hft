Cubist Test
===========

Start Time: 07:12 24th Oct

We need to develop a backtesting system to evaluate the PnL of some high frequency strategies, and
particularly the impact of the latency of our connectivity to the exchange on their performance.
The strategy is trading a single stock. 

Price time priority.
Orders are all day-limit.
No impact on order book liquidity (market does not react to any actions we perform).

Your job is to design and implement this backtesting system:
• Design: define the exact perimeter of what your system will do, clarify things if necessary (there
are some little details in the previous text which are voluntarily not perfectly defined), design
the architecture, define how the strategies should interact with the system
• Implement: implement what you have designed in C++ and provide a little strategy to test it

Input Format:
=============
The user configures the backtesting system with the latency of us to exchange and exchange to us and then 
can run it on the marketdata. The format of the marketdata is:

utc_nanosecs, action, order_id, side, qty, price
12344555, ADD, 123, B, 10, 100
12344556, ADD, 156, S, 9, 100
1234455, DELETE

utc_nanosecs The action time in nanosecs since Unix epoch
action ADD / UPDATE / DELETE
An UPDATE can only be applied to the qty: it is
either reduced or increased. An order’s price or
side is never changed (if someone wants to
change the price, they have to delete the order
and issue a new one)
order_id unique order identifier. It is never changed, it’s
not reused for orders, etc: it’s perfectly unique.
side B: Buy, S: Sell
price
qty

Output Artifacts:
=================
Trades: All the trades that have occurred for the user.
The pnl of the trades.

Components:
===========


Backtester:
-----------
As the backtesting system developer, you don’t know the strategies. Your system should allow a strategy
to post an order, update or delete it, and receives some potential trades.
The strategy can trade at any
time (usually depending on the market moves). The strategy can also request the simulation time (in the
simulated world) during the simulation.
From the trader’s point of view, the use case for this system is simple: the trader develops a strategy
and then run it in the backtesting environment. From the trades and the market data the strategy
receives, the trader can compute a PnL. That’s it. Note that the order book history is huge (let’s say it
covers years of history): the trader wants his simulation to run as fast as possible.

Components in our strategy.

- Queuer
- Orderbook Matcher
- Logger

Interface?
post, update, delete.
every time you post or update an order it should give you a list of all the trades. 

Do we want a running pnl calculation or a final pnl calculation? (think its easy to have both).
Just log all trades and run pnl on it.




Strategy
--------
Choices: Full L3 orderbook? To model the queue position and getting cancelled etc.
This implies that for the marketdata as well we shall need the full L3 position.
Assume the strategy starts off with 0 inventory.
Strategy needs to be able to query the orderbook and also get called on every update.



Marketdata Simulator
--------------------
Have marketdata resembling cases for the exchange but also take actual marketdata and play it out
for different types of products. Are we doing L3 or L2 marketdata?
If we take L3 that assumes a certain orderbook. We shall have to make our strategy consistent so 
that executing a trade doesn't screw up the orderbook. The main problem is when there is an
aggressive order and there is matching with ours. Or if there is a limit that crosses us if we are
at the top of the book.
How do we treat the effect on the L3 data?
We need the orderbook to be consistent before and after.
This means that it would be hard to use actual marketdata.

Option 2:
Just use agents placing orders on the markets.
Make them adversarial.




Data logging & Analysis:
=========================

Experiments:
------------
Have the same bot with slower and fast latencies, competing with us.
Have no other agents in the market.
Have a trending market.
Have a low vol market.

For each of the above, try different types of latencies.
What types of latencies? They should obviously be relative.

The latency from our server to the exchange: this is the time it takes for a message sent by our
trading server to reach the exchange
• The latency from the exchange to our server: this is the time it takes a message sent by the
exchange to reach our trading server
• We will assume that the exchange latency itself is zero: when it receives a message, the
exchange matching engine processes it instantly. 


Testing
=======


Performance Considerations:
===========================
In order to scale this backtester out use multithreading/processing to run for seperate days.



Design Decisions:
=================
Fixed latencies for all paths to exchange including the public feed and the private feed.
Strategy only relies on L1 marketdata.

Real hft strategies would need to consider their execution far more carefully as well as manage
stacks of orders. They would need to also obey exchange limitations and threshold such as risk
checks, message limits, invalid orders.
The execution strategy is also important. How do you execute? You execute passively or aggressively?
They would also need to handle exchange timeouts and cancel on disconnect logic.
Usually strategies would also limit how much inventory they could hold and then will start
aggressively shedding their inventory when they are holding too much.
The pnl also consists of a large number of costs, like trading fees, borrow costs, marketdata costs,

Also communication with the exchange usually involves a larger number of messages and states

If the connection falls too far behind the exchange would usually terminate the connection.
We would need to handle reconnection to exchanges as well.


With respect to the marketdata lagging behind the backtester state:
Implement a Look-Ahead Mechanism: While it's not always realistic for live trading, in backtesting, you can implement a look-ahead mechanism. If a market data update is about to modify or remove an order that the strategy has aggressed against, delay or adjust the strategy's action accordingly. This ensures that the strategy operates on a realistic state of the market.

Fallback Strategy: If you detect that an order in your market data has been modified or removed due to the strategy's actions, and it hasn't been accounted for yet, consider a fallback strategy. For instance, if the strategy's order should've been matched against a bid that's no longer present, you can:

a. Match it against the next best bid (if it's an aggressive order).

b. Cancel the strategy's order and log the event for later analysis.