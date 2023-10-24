cmake_minimum_required(VERSION 3.26)
project(millen)
project(cubist)

set(CMAKE_CXX_STANDARD 17)

add_executable(millen main.cpp)

add_executable(cubist
        cubist/Strategy.h
        cubist/Backtester.h
        cubist/ring_buffer.h
        cubist/mytypedefs.h
        cubist/L3OrderBook.h
        cubist/Backtester.cpp)
