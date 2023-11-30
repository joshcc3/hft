#ifndef HFT_RING_BUFFER_H
#define HFT_RING_BUFFER_H

//#define NDEBUG


#include <cstddef>
#include <cassert>
#include <memory>
#include <cstdlib>

template<typename T>
struct Free {
    void operator()(T *ptr) {
        free(ptr);
    }
};

template<typename T, std::size_t Capacity>
class RingBuffer {
public:
    static_assert(__builtin_popcount(Capacity) == 1);
    RingBuffer() : data{static_cast<T *>(malloc(sizeof(T) * Capacity))}, head{0}, tail{0}, _size{0} {
        if (data.get() == nullptr) {
            throw std::bad_alloc();
        }
    }

    static_assert(Capacity > 0, "Capacity must be positive.");

    [[nodiscard]] bool empty() const noexcept {
        return head == tail;
    }

    [[nodiscard]] bool full() const noexcept {
        return _size == Capacity;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        assert(check());
        return _size;
    }

    template<typename... Args>
    void emplace_back(Args &&... args) {
        assert(!full());
        assert(check());

        new(&data[tail]) T(std::forward<Args>(args)...);
        tail = next(tail);
        ++_size;
    }

    const T &front() const {
        assert(!empty());
        assert(check());

        return data[head];
    }

    const T& get(int i) const {
        return data[(head + i) & (Capacity - 1)];
    }

    void pop() {
        assert(!empty());
        assert(check());

        data[head].~T();

        head = next(head);
        --_size;
    }

private:
    std::unique_ptr<T[], Free<T>> data;
    std::size_t head;
    std::size_t tail;
    std::size_t _size;

    [[nodiscard]] std::size_t next(std::size_t current) const noexcept {
        return (current + 1) % Capacity;
    }

    [[nodiscard]] bool check() const {
        assert(head < Capacity);
        assert(tail < Capacity);
        if (tail > head) {
            assert(_size == tail - head);
        } else if(tail < head) {
            assert(_size == Capacity + tail - head);
        } else {
            assert(_size == Capacity || _size == 0);
        }
        return true;
    }
};


#endif //HFT_RING_BUFFER_H
