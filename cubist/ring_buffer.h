#ifndef HFT_RING_BUFFER_H
#define HFT_RING_BUFFER_H


#include <cstddef>
#include <vector>
#include <cassert>
#include <memory>

// TODO use a unique_ptr for an array here instead of a vector.

template<typename T, std::size_t Capacity>
class ring_buffer {
public:

    ring_buffer() : head{0}, tail{0} {
        data.reserve(Capacity);
    }

    static_assert(Capacity > 0, "Capacity must be positive.");

    [[nodiscard]] bool empty() const noexcept {
        return head == tail;
    }

    [[nodiscard]] bool full() const noexcept {
        return next(tail) == head;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        check();
        if (tail >= head) {
            return tail - head;
        } else {
            return Capacity + tail - head;
        }
    }

    template<typename... Args>
    void emplace_back(Args &&... args) {
        assert(!full());
        check();

        new(&data[tail]) T(std::forward<Args>(args)...);  // placement new
        tail = next(tail);
    }

    T &front() {
        assert(!empty());
        check();

        return data[head];
    }

    const T &front() const {
        assert(!empty());
        check();

        return data[head];
    }

    void pop() {
        assert(!empty());
        check();

        head = next(head);
    }

private:
    std::vector<T> data;
    std::size_t head;
    std::size_t tail;

    [[nodiscard]] std::size_t next(std::size_t current) const noexcept {
        return (current + 1) % Capacity;
    }

    void check() const {
        assert(head < Capacity);
        assert(tail < Capacity);
    }
};


#endif //HFT_RING_BUFFER_H
