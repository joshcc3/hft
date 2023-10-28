//
// Created by jc on 27/10/23.
//
#include "simple_server.cpp"
#include "epoll_test.cpp"
#include "io_uring_eg.cpp"

int main() {
//    epoll_test();
//    simpleServer();
    io_uring_eg();
}