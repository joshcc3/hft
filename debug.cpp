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

using namespace std;


int main() {

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
}
