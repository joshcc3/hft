//
// Created by joshuacoutinho on 15/12/23.
//

#ifndef CONTAINERS_H
#define CONTAINERS_H
#include <cstring>
#include "defs.h"
#include "IGB82576Interop.h"
#include <new>
#include <cassert>
#include <utility>

namespace josh {
    template<template<typename, int> class C, typename X, int Sz>
    class iter {
    public:
        C<X, Sz>& c;
        int ix;

        iter(C<X, Sz>& c, int ix): c{c}, ix{ix} {
            c.stateCheck();
            assert(ix >= -1 && ix <= Sz);
        }

        iter(const iter<C, X, Sz>& i): c{i.c}, ix{i.ix} {
            c.stateCheck();
            assert(ix >= -1 && ix <= Sz);
        }

        iter& operator=(const iter<C, X, Sz>& i) {
            assert(c.memory == i.c.memory);
            ix = i.ix;
            c.stateCheck();
            assert(ix >= -1 && ix <= Sz);
            return *this;
        }

        iter operator-(int a) const {
            assert(ix >= a);
            return {c, ix - a};
        }

        bool operator!=(const iter& iter) const {
            return iter.c.memory != c.memory || iter.ix != ix;
        }

        void operator++() {
            assert(ix < Sz);
            ++ix;
        }

        iter operator+(int a) const {
            assert(ix + a <= Sz);
            return {c, ix + a};
        }

        X& operator*() const {
            assert(ix >= 0 && ix < Sz);
            return c[ix];
        }

        X* operator->() const {
            assert(ix >= 0 && ix < Sz);
            return &c[ix];
        }
    };

    template<template<typename, int> class C, typename X, int Sz>
    class riter {
    public:
        C<X, Sz>& c;
        int ix;

        riter(C<X, Sz>& c, int ix): c{c}, ix{ix} {
        }


        bool operator!=(const riter<C, X, Sz>& iter) const {
            return iter.c.memory != c.memory || iter.ix != ix;
        }

        X& operator*() const {
            assert(ix <= Sz && ix >= 0);
            return c[ix];
        }


        X* operator->() const {
            assert(ix <= Sz && ix >= 0);
            return &c[ix];
        }

        riter operator-(int a) const {
            assert(ix + a < Sz);
            return {c, ix + a};
        }

        void operator++() {
            assert(ix >= 0);
            --ix;
        }


        iter<C, X, Sz> base() const {
            assert(ix >= -1 && ix < Sz);
            return {c, ix + 1};
        }
    };

    template<int SzBits>
    class alignas(64) bitset {
    public:
        u64* memory;
        static constexpr u64 wordMask = 63;
        static constexpr u64 MEM_SZ_BYTES = (SzBits + 63) / 64 * 8;;
        u32 anyCount = 0;

        bitset(): memory(static_cast<u64 *>(malloc(MEM_SZ_BYTES))) {
            assert(memory != nullptr);
	        assert(MEM_SZ_BYTES * 8 >= SzBits && MEM_SZ_BYTES * 8 < SzBits + 63);
	        for(int i = 0; i < MEM_SZ_BYTES/8; ++i) {
		        memory[i] = 0;
	        }
        }
	// TODO - deallocate the memory

        void set(int ix) {
            assert(ix < SzBits);
            u64 word = (1 << (wordMask & ix));
            assert(word != 0);
            u8 prev = __builtin_popcount(memory[ix >> 6]);
            memory[ix >> 6] |= word;
            u8 after = __builtin_popcount(memory[ix >> 6]);
            assert(prev == after || prev + 1 == after);
            anyCount += after - prev;
        }

        bool get(int ix) const {
            assert(ix < SzBits);
            return (memory[ix >> 6] >> (wordMask & ix)) & 1;
        }


        void reset(int ix) {
            assert(ix < SzBits);
            u64 word = (1 << (wordMask & ix));
            assert(word != 0);
            u8 prev = __builtin_popcount(memory[ix >> 6]);
            memory[ix >> 6] &= ~word;
            u8 after = __builtin_popcount(memory[ix >> 6]);
            anyCount += after - prev;
        }

        bool any() const {
            return anyCount != 0;
        }
    };

    template<typename T, int Sz>
    class alignas(64) array {
    public:
       // TODO - deallocate the memory
        T* const memory;
        u64 overwriteWord{};
        int sz;

        using arriter = iter<array, T, Sz>;

        array(): memory(reinterpret_cast<T *>(malloc(Sz * sizeof(T)))), sz{0} {
            assert(sizeof(T) == 32);
            stateCheck();
        }

        using veciter = iter<array, T, Sz>;
        using vecriter = riter<array, T, Sz>;

        void clear() {
            stateCheck();
            for (int i = 0; i < sz; ++i) {
                memory[i].~T();
            }
            sz = 0;
        }

        size_t capacity() const {
            return Sz;
        }

        size_t size() const {
            return sz;
        }

        bool empty() const {
            return sz == 0;
        }

        T& operator[](size_t ix) const {
            assert(ix < Sz && ix >= 0);
            stateCheck();
            return memory[ix];
        }

        T& back() const {
            stateCheck();
            assert(sz > 0);
            return memory[sz - 1];
        }

        T& front() const {
            stateCheck();
            assert(sz > 0);
            return memory[0];
        }

        veciter begin() {
            return {*this, 0};
        }

        veciter end() {
            return {*this, sz};
        }

        vecriter rbegin() {
            return {*this, sz - 1};
        }

        vecriter rend() {
            return {*this, -1};
        }

        template<typename... Args>
        void insert(const iter<array, T, Sz>& loc, Args&&... args) {
            stateCheck();
            assert(loc.ix >= 0);
            assert(loc.ix <= sz);
            assert(sz < Sz);

            int destIx = loc.ix;
            int numToCp = sz - destIx;

            T* memoryLoc = &memory[destIx];
            int isEven = 1 - (numToCp & 1);
            u64* _destLoc = reinterpret_cast<u64 *>(memoryLoc + numToCp - isEven);
            u64* _srcLoc = reinterpret_cast<u64 *>(memoryLoc + numToCp - 1 - isEven);
            for (int i = 0; i < numToCp / 2 + (1 - isEven); ++i) {
                *_destLoc = *_srcLoc;
                --_destLoc;
                --_srcLoc;
            }
            // TODO - likely issue is with alignment
            new(memoryLoc) T(std::forward<Args>(args)...);
            ++sz;
        }


        void erase(const iter<array, T, Sz>& loc) {
            stateCheck();
            assert(loc.ix >= 0);
            assert(loc.ix < sz);
            assert(sz > 0);

            T* memoryLoc = &memory[loc.ix];
            memoryLoc->~T();
            int numToCp = sz - loc.ix - 1;
            u64* _dst = reinterpret_cast<u64 *>(memoryLoc);
            u64* _src = reinterpret_cast<u64 *>(memoryLoc + 1);
            for (int i = 0; i < numToCp / 2 + 1; ++i) {
                *_dst = *_src;
                ++_dst;
                ++_src;
            }
            --sz;
            assert(numToCp <= sz);
        }

        void stateCheck() const {
            assert(sz < Sz);
            assert(nullptr != memory);
        }
    };

    template<typename _InputIterator, typename _Predicate>
    _GLIBCXX20_CONSTEXPR
    inline _InputIterator
    find_if(_InputIterator iter, _InputIterator last, _Predicate pred) {
        while (iter != last && !pred(*iter)) {
            ++iter;
        }
        return iter;
    }

    template<typename _II, typename _OI>
    void copy(_II first, _II last, _OI result) {
        while (first != last) {
            *result = *first++;
        }
    }


    template<template<class, int> class V, typename T, int Sz>
    bool operator==(const riter<V, T, Sz>& iter1, const riter<V, T, Sz>& iter2) {
        return iter1.c.memory == iter2.c.memory && iter1.ix == iter2.ix;
    }


    template<template<class, int> class V, typename T, int Sz>
    bool operator!=(const riter<V, T, Sz>& iter1, const riter<V, T, Sz>& iter2) {
        return iter1.c.memory != iter2.c.memory || iter1.ix != iter2.ix;
    }

    template<template<class, int> class V, typename T, int Sz>
    bool operator==(const iter<V, T, Sz>& iter1, const iter<V, T, Sz>& iter2) {
        return iter1.c.memory == iter2.c.memory && iter1.ix == iter2.ix;
    }


    template<template<class, int> class V, typename T, int Sz>
    bool operator!=(const iter<V, T, Sz>& iter1, const iter<V, T, Sz>& iter2) {
        return iter1.c.memory != iter2.c.memory || iter1.ix != iter2.ix;
    }
}


#endif //CONTAINERS_H
