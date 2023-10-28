#ifndef HFT_RING_BUFFER_H
#define HFT_RING_BUFFER_H

#define NDEBUG


#include <cstddef>
#include <vector>
#include <cassert>
#include <memory>


template<typename T>
struct Free {
    void operator()(T *ptr) {
        std::free(ptr);
    }
};

template<typename T, std::size_t Capacity>
class RingBuffer {
public:

    RingBuffer() : data{static_cast<T *>(malloc(sizeof(T) * Capacity))}, head{0}, tail{0} {
        if (data == NULL) {
            throw std::bad_alloc();
        }
    }

    static_assert(Capacity > 0, "Capacity must be positive.");

    [[nodiscard]] bool empty() const noexcept {
        return head == tail;
    }

    [[nodiscard]] bool full() const noexcept {
        return next(tail) == head;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        assert(check());
        if (tail >= head) {
            return tail - head;
        } else {
            return Capacity + tail - head;
        }
    }

    template<typename... Args>
    void emplace_back(Args &&... args) {
        assert(!full());
        assert(check());

        new(&data[tail]) T(std::forward<Args>(args)...);
        tail = next(tail);
    }

    const T &front() const {
        assert(!empty());
        assert(check());

        return data[head];
    }

    void pop() {
        assert(!empty());
        assert(check());

        data[head].~T();

        head = next(head);
    }

private:
    std::unique_ptr<T[], Free<T>> data;
    std::size_t head;
    std::size_t tail;

    [[nodiscard]] std::size_t next(std::size_t current) const noexcept {
        return (current + 1) % Capacity;
    }

    bool check() const {
        assert(head < Capacity);
        assert(tail < Capacity);
        return true;
    }
};


#endif //HFT_RING_BUFFER_H
