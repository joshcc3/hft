//
// Created by jc on 30/10/23.
//

#ifndef TICTACTOE_SPSC_H
#define TICTACTOE_SPSC_H

#include <cstdint>
#include <atomic>
#include <vector>
#include "mytypedefs.h"
#include <memory>
#include <iostream>

using i32 = int32_t;

template<typename T, int Capacity>
class SPSC {
public:
    SPSC();
    ~SPSC();

    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] bool full() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    void push_back(T &&arg);

    T pop();

private:

    using Index = i32;
    using ContainerT = std::unique_ptr<T[]>;

    std::atomic<Index> head;
    std::atomic<Index> tail;
    ContainerT container;

};

template<typename T, int C>
SPSC<T,C>::~SPSC() {
    std::cout << "Destructor called" << std::endl;
    container.~ContainerT();
}

template<typename T, int C>
SPSC<T, C>::SPSC(): head{0}, tail{0}, container{std::make_unique<T[]>(C)} {

}

template<typename T, int C>
bool SPSC<T, C>::empty() const noexcept {
    int h = head.load(std::memory_order_relaxed);
    int t = tail.load(std::memory_order_relaxed);
    assert(h <= t);
    return h == t;
}

template<typename T, int C>
bool SPSC<T, C>::full() const noexcept {
    int h = head.load(std::memory_order_relaxed);
    int t = tail.load(std::memory_order_relaxed);
    assert(h <= t);
    return t - h == C;
}

template<typename T, int C>
size_t SPSC<T, C>::size() const noexcept {
    int h = head.load(std::memory_order_relaxed);
    int t = tail.load(std::memory_order_relaxed);
    assert(h <= t);
    return t - h;
}

template<typename T, int C>
void SPSC<T, C>::push_back(T &&arg) {
    int ogSize = size();
    Index h = head.load(std::memory_order_acquire);
    Index t = tail.load(std::memory_order_relaxed);
    assert(h <= t);
    while (t - h == C) {
        h = head.load(std::memory_order_acquire);
        t = tail.load(std::memory_order_relaxed);
    }

    container[t % C] = move(arg);

    tail.store(t + 1, std::memory_order_release);

    assert(size() <= ogSize + 1);
    assert(tail.load(std::memory_order_relaxed) == t + 1);
    assert(head.load(std::memory_order_relaxed) >= h);
}

template<typename T, int C>
T SPSC<T, C>::pop() {
    int ogSize = size();

    Index t = tail.load(std::memory_order_acquire);
    Index h = head.load(std::memory_order_relaxed);

    if (h == t) {
        return "";
    }

    T &v = container[h % C];

    T res = std::move(v);
    v.~T();

    head.store(h + 1, std::memory_order_release);

    assert(size() >= ogSize - 1);
    assert(tail.load(std::memory_order_relaxed) >= t);
    assert(head.load(std::memory_order_relaxed) == h + 1);

    return res;
}


#endif //TICTACTOE_SPSC_H
