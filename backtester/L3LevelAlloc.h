//
// Created by jc on 28/10/23.
//

#ifndef OBTESTER_L3LEVELALLOC_H
#define OBTESTER_L3LEVELALLOC_H

#include <memory>
#include <cstdlib>

using namespace std;

template<typename T>
struct L3LevelAlloc : std::allocator<T> {

    constexpr static int CAPACITY = 50;

    using value_type = T;
    using reference = T &;
    using pointer = T *;
    using const_pointer = const T *;
    using const_reference = const T &;
    using size_type = unsigned long;
    using difference_type = ptrdiff_t;

    L3LevelAlloc() = default;

    L3LevelAlloc(const L3LevelAlloc &) = default;

    L3LevelAlloc(L3LevelAlloc &) = default;

    L3LevelAlloc(L3LevelAlloc &&) = default;

    L3LevelAlloc &operator=(L3LevelAlloc &) = delete;

    pointer address(reference t);

    const_pointer address(const_reference t) const;

    pointer allocate(size_type n, const_pointer = 0) const;

    void deallocate(pointer t, size_type sz);

    size_type max_size() const;

    template<typename U>
    struct rebind {
        typedef L3LevelAlloc<U> other;
    };

private:

    struct Free {
        void operator()(T *ptr) const {
            if(ptr != nullptr) {
                std::free(ptr);
            }
            pool = nullptr;
            poolSize = -1;
            ptr = nullptr;
        }
    };

    using PoolT = unique_ptr<T, Free>;

    static PoolT pool;
    static int poolSize;



};

template<typename T>
typename L3LevelAlloc<T>::PoolT L3LevelAlloc<T>::pool{
        static_cast<T *>(std::malloc(L3LevelAlloc<T>::CAPACITY * sizeof(T))),
        L3LevelAlloc<T>::Free()
};

template<typename T>
int L3LevelAlloc<T>::poolSize = 0;

template<typename T>
typename L3LevelAlloc<T>::pointer L3LevelAlloc<T>::address(reference t) {
    assert(&t >= pool.get() && &t - pool.get() < poolSize);
    return &t;
}

template<typename T>
typename L3LevelAlloc<T>::const_pointer L3LevelAlloc<T>::address(typename L3LevelAlloc<T>::const_reference t) const {
    assert(&t >= pool.get() && &t - pool.get() < poolSize);
    return &t;
}



// potentially use the hint

// L3LevelAlloc<std::pair<long, Order> >::allocate(unsigned long, std::pair<long, Order> const*) const
template<typename T>
typename L3LevelAlloc<T>::pointer
L3LevelAlloc<T>::allocate(typename L3LevelAlloc<T>::size_type n, typename L3LevelAlloc<T>::const_pointer hint) const {
    int sz = poolSize;
    assert(sz + n <= CAPACITY);
    pointer allocation = pool.get() + poolSize;
    poolSize += n;
    assert(sz + n == poolSize);
    assert(allocation != nullptr);
    return allocation;

}

template<typename T>
void L3LevelAlloc<T>::deallocate(L3LevelAlloc<T>::pointer t, size_type sz) {
    assert(t <= pool.get() + poolSize - 1);
    assert(t == pool.get() + poolSize - 1);
    poolSize -= sz;
}

template<typename T>
inline typename L3LevelAlloc<T>::size_type L3LevelAlloc<T>::max_size() const {
    return CAPACITY;
}


#endif //OBTESTER_L3LEVELALLOC_H
