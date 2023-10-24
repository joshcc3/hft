//
// Created by jc on 24/10/23.
//

#ifndef HFT_RING_BUFFER_H
#define HFT_RING_BUFFER_H


#include <cstddef>
#include <array>
#include <cassert>

template <typename T, std::size_t Capacity>
class ring_buffer {
public:
    static_assert(Capacity > 0, "Capacity must be positive.");

    [[nodiscard]] bool empty() const noexcept {
        return head_ == tail_;
    }

    [[nodiscard]] bool full() const noexcept {
        return next(tail_) == head_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        if (tail_ >= head_) {
            return tail_ - head_;
        } else {
            return Capacity + tail_ - head_;
        }
    }

    void push_back(const T& value) {
        assert(!full());

        data_[tail_] = value;
        tail_ = next(tail_);
    }

    void push_back(T&& value) {
        assert(!full());

        data_[tail_] = std::move(value);
        tail_ = next(tail_);
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        assert(!full());

        new (&data_[tail_]) T(std::forward<Args>(args)...);  // placement new
        tail_ = next(tail_);
    }

    T& front() {
        assert(!empty());

        return data_[head_];
    }

    const T& front() const {
        assert(!empty());

        return data_[head_];
    }

    void pop() {
        assert(!empty());

        head_ = next(head_);
    }

private:
    std::array<T, Capacity> data_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;

    [[nodiscard]] std::size_t next(std::size_t current) const noexcept {
        return (current + 1) % Capacity;
    }
};

// Example usage:
// ring_buffer<int, 5> buffer;
// buffer.push_back(42);
// buffer.emplace_back(43);
// int front = buffer.front();
// buffer.pop();



#endif //HFT_RING_BUFFER_H
