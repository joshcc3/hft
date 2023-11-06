//
// Created by jc on 25/10/23.
//


#include <iostream>
#include <algorithm>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <chrono>
#include <cassert>
#include <deque>
#include <x86intrin.h>
#include <bitset>

using namespace std;

// tODO - always_destroy

using u64 = uint64_t;
using u32 = uint32_t;

template<typename T>
void p() {
    cout << typeid(T).name() << endl;
}

int main() {
    vector<int> a{1,2,3};
    int p = 4;
    cout << *a.rbegin() << endl;
}


struct A {

    int x;

    template<bool IsConst = false>
    auto getValue() -> std::conditional_t<IsConst, const int &, int &> {
        if constexpr (IsConst) {
            return x;
        } else {
            return x;
        }
    }


    //    template <bool IsConst = false>
//    auto getValue() -> std::conditional_t<IsConst, const int&, int&> {
//        if constexpr (IsConst) {
//            std::cout << "const version\n";
//            return si;
//        } else {
//            std::cout << "non-const version\n";
//            return value;
//        }
//    }
};

struct C {
    static int x;

    C() {
        cout << "DC: " << ++C::x << endl;
    }

    C(const C &cp) {
        cout << "CC: " << ++C::x << endl;
    }

    C &operator=(const C &a) {
        if (this == &a) {
            return *this;
        }
        cout << "AC: " << ++C::x << endl;
        return *this;
    }

    C(const C &&a) = delete;

    C(C &&a) = delete;

    C &operator=(C &&a) = delete;

    void f() {

    }

    ~C() {
        --C::x;
    }
};

int C::x = 0;


int main123() {
    using P = pair<int, C>;
    map<int, P> mp;
    mp.emplace(0, P{});
    cout << "Mp constructed" << endl;
    mp.find(0)->second.second.f();
    cout << "Completed" << endl;
}


int main1() {

    for (int SZ = 10; SZ <= 200; ++SZ) {
        int ITERS = 100'000;

        using Dat = pair<long, long>;
        srandom(0xff38423d);

        vector<Dat> v;

        for (int i = 0; i < SZ; ++i) {
            v.emplace_back(pair<int, int>{random(), 1});
        }
        unordered_map<long, long> mp(v.begin(), v.end());

        vector<long> deleteElems;
        deleteElems.reserve(SZ);
        for (int i = 0; i < SZ; ++i) {
            int nextElem;
            while (find(deleteElems.begin(), deleteElems.end(), nextElem = v[random() % SZ].first) !=
                   deleteElems.end());
            deleteElems.push_back(nextElem);
        }

        {
            long timeNs = 0;
            // using lambda 30ms
            for (int i = 0; i < ITERS; i += SZ) {
                deque<Dat> v2;
                for (int i = 0; i < v.size(); ++i) {
                    v2.emplace_back(v[i]);
                }
                chrono::time_point s = chrono::system_clock::now();
                for (int i = 0; i < v.size(); ++i) {
                    v2.erase(std::find(v2.begin(), v2.end(), Dat{deleteElems[i], 1}));
                }
                assert(v2.empty());
                chrono::time_point e = chrono::system_clock::now();
                timeNs += chrono::duration_cast<chrono::nanoseconds>(e - s).count();;
            }
            cout << SZ << ",Vec," << double(timeNs) / ITERS << endl;
        }

        {
            long timeNs = 0;
            // using lambda 30ms
            for (int i = 0; i < ITERS; i += SZ) {
                unordered_map<long, long> v2(v.begin(), v.end());
                chrono::time_point s = chrono::system_clock::now();
                for (int i = 0; i < v.size(); ++i) {
                    v2.erase(deleteElems[i]);
                }
                assert(v2.empty());
                chrono::time_point e = chrono::system_clock::now();
                timeNs += chrono::duration_cast<chrono::nanoseconds>(e - s).count();;
            }
            cout << SZ << ",Map," << double(timeNs) / ITERS << endl;
        }
    }

    return 0;
}
