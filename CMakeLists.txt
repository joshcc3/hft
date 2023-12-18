cmake_minimum_required(VERSION 3.26)
project(backtester)
project(obtester)
project(tictactoe)
project(lll_strategy)
project(lll_exchange)
project(xdp_test)
set(CMAKE_CXX_STANDARD 20)


find_package(Boost 1.78.0 COMPONENTS iostreams REQUIRED)

add_executable(lll_exchange
        lowlatencylab/server/exchmcserver.cpp
        lowlatencylab/server/exchmcserver.h)

add_executable(lll_strategy
#        lowlatencylab/client/mdmcclient.cpp
        lowlatencylab/client/mdmcclient.h
        lowlatencylab/client/mdmcclient.cpp
        lowlatencylab/client/L2OB.cpp
        lowlatencylab/client/L2OB.h
        lowlatencylab/defs.h
        lowlatencylab/client/Strat.h
        lowlatencylab/client/Strat.cpp
        lowlatencylab/client/defs.h
        cppkern/containers.h
        cppkern/containers.cpp
        lowlatencylab/client/CoroutineMngr.h
        lowlatencylab/client/CoroutineMngr.cpp
        lowlatencylab/client/launch.h
        lowlatencylab/client/launch.cpp
        cppkern/IGB82576Interop.h
        # lowlatencylab/client/IGB82576IO.h
        # lowlatencylab/client/IGB82576IO.cpp
        # cppkern/containers.h
        # cppkern/containers.cpp
        # cppkern/kernlib.h
)


#include_directories(../linux/include/)
#include_directories(../linux/)
#include_directories(../linux/arch/x86/include)
#include_directories(../linux/arch/x86/include/generated)

add_executable(backtester
        backtester/Strategy.h
        backtester/Backtester.h
        backtester/RingBuffer.h
        backtester/mytypedefs.h
        backtester/L3OrderBook.h
        backtester/Backtester.cpp
        backtester/BacktestListener.cpp
        backtester/Strategy.cpp
        backtester/L3LevelAlloc.h
        backtester/L3LevelAlloc.cpp
        backtester/main.cpp
        backtester/SPSC.cpp
        backtester/SPSC.h
)
add_executable(networking networking/epoll_test.cpp
        networking/simple_server.cpp
        networking/main.cpp
        networking/io_uring_eg.cpp
        networking/io_uring_eg.cpp
        networking/select_io.h)

add_executable(debug
        debug.cpp)

add_executable(obtester
        backtester/orderbook_tester.cpp
        backtester/Strategy.h
        backtester/mytypedefs.h
        backtester/L3OrderBook.h
        backtester/L3LevelAlloc.h
        backtester/L3LevelAlloc.cpp
)

add_executable(xdp_test
        lowlatencylab/defs.h
        lowlatencylab/client/XDPIO.cpp
        lowlatencylab/client/XDPIO.h
        lowlatencylab/client/TCPStack.cpp
        lowlatencylab/client/TCPStack.h
)

target_link_libraries(networking uring)
target_link_libraries(backtester Boost::iostreams)
target_link_libraries(lll_exchange Boost::iostreams uring)
target_link_libraries(lll_strategy uring xdp bpf)
target_link_libraries(xdp_test xdp bpf)
