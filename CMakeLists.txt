cmake_minimum_required(VERSION 3.26)
project(cubist)
project(obtester)
set(CMAKE_CXX_STANDARD 17)


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

target_link_libraries(networking uring)