cmake_minimum_required(VERSION 3.26)
project(cubist)
project(obtester)
project(tictactoe)
project(lll_strategy)
project(lll_exchange)
project(xdp_test)
set(CMAKE_CXX_STANDARD 17)

find_package(Boost 1.78.0 COMPONENTS iostreams REQUIRED)

add_executable(lll_exchange
        lowlatencylab/server/exchmcserver.cpp
        lowlatencylab/server/exchmcserver.h)

add_executable(lll_strategy
        lowlatencylab/client/mdmcclient.cpp
        lowlatencylab/client/mdmcclient.h
        lowlatencylab/client/L2OB.cpp
        lowlatencylab/client/L2OB.h
        lowlatencylab/defs.h
        lowlatencylab/client/Strat.h
        lowlatencylab/client/XDPIO.cpp
        lowlatencylab/client/XDPIO.h
)

add_executable(cubist
        cubist/Strategy.h
        cubist/Backtester.h
        cubist/RingBuffer.h
        cubist/mytypedefs.h
        cubist/L3OrderBook.h
        cubist/Backtester.cpp
        cubist/BacktestListener.cpp
        cubist/Strategy.cpp
        cubist/L3LevelAlloc.h
        cubist/L3LevelAlloc.cpp
        cubist/main.cpp
        cubist/SPSC.cpp
        cubist/SPSC.h
)
add_executable(networking networking/epoll_test.cpp
        networking/simple_server.cpp
        networking/main.cpp
        networking/io_uring_eg.cpp
        networking/io_uring_eg.cpp
        networking/select_io.h)

add_executable(debug debug.cpp)

add_executable(obtester
        cubist/orderbook_tester.cpp
        cubist/Strategy.h
        cubist/mytypedefs.h
        cubist/L3OrderBook.h
        cubist/L3LevelAlloc.h
        cubist/L3LevelAlloc.cpp
)

add_executable(xdp_test
        ebpf/lll-1/main.cpp
        ebpf/lll-1/defs.h
)

target_link_libraries(networking uring)
target_link_libraries(cubist Boost::iostreams)
target_link_libraries(lll_exchange Boost::iostreams uring)
target_link_libraries(lll_strategy uring xdp bpf)
target_link_libraries(xdp_test xdp bpf)
