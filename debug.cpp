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

int scorePos(string board[3], int i, int j) {

    if (board[i][j] != '.') {
        return -30;
    }

    int rowCount = 0;
    int colCount = 0;
    int diagCount1 = 0;
    int diagCount2 = 0;
    for (int x = 0; x < 3; x++) {
        rowCount += (board[i][x] == 'X') - (board[i][x] == 'O');
        colCount += (board[x][j] == 'X') - (board[x][j] == 'O');
        diagCount1 += (board[x][x] == 'X') - (board[x][x] == 'O');
        diagCount2 += (board[2 - x][x] == 'X') - (board[2 - x][x] == 'O');
    }

    diagCount1 = i == j ? diagCount1 : 0;
    diagCount2 = i + j == 2 ? diagCount2 : 0;

    if (max((rowCount), max((colCount), max((diagCount1), (diagCount2)))) == 2) {
        return 30;

    } else if (max(abs(rowCount), max(abs(colCount), max(abs(diagCount1), abs(diagCount2)))) == 2) {
        return 29;
    } else {
        int score = rowCount + colCount + diagCount1 + diagCount2;
        return 2*abs(score) - (score < 0);
    }

}

void solveTicTacToe(string board[3]) {
    int score = -30;
    pair<int, int> result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            int s = scorePos(board, i, j);
            if (s > score) {
                result.first = i;
                result.second = j;
                score = s;
            }
        }
    }

    cout << result.first << " " << result.second << endl;
}

using u64 = uint64_t;


struct Position {
    int x;
    int y;
};

struct State {
    const vector<Position> positions = {Position{0, 0}, Position{0, 0}, Position{0, 0}, Position{0, 0}, Position{0, 0}, Position{0, 0}, Position{0, 0}};
    constexpr static int DOT = 0;
    constexpr static int X = 1;
    constexpr static int O = 2;
    u64 internalState;

    bool isOpen(const Position& p) {
        return getPos(p) == DOT;
    }

    void place(const Position& p) {
        u64 mask = 0;
        u64 placed = 0;
        assert(false);
        internalState = (internalState & mask) | placed;
    }

    void revert(const Position& p) {
        u64 mask = 0;
        u64 placed = 0;
        assert(false);
        internalState = (internalState & mask) | placed;

    }

private:
    u64 getPos(const Position& p) {
        u64 res = (internalState >> ((p.x * 3 + p.y)*2)) & 3;
        assert(res >= DOT && res <= O);
        return res;
    }

};

struct StateHash
{
    std::size_t operator()(const State& s) const noexcept
    {
        return s.internalState;
    }
};


bool operator==(const State& s1, const State& s2) {
    return s1.internalState == s2.internalState;
}

class Cache {
    unordered_map<State, int, StateHash> mp;
};


int calculateOutcome(State& state, Cache& cache) {
    const auto& iter = cache.find(state);
    if(iter != cache.end()) {
        return iter->second;
    }
    for(const Position& p : state.positions) {
        if(state.isOpen(p)) {
            State prev = state;
            state.place(p);
            int outcome = calculateOutcome(state, cache);
            assert(cache.find(state) != cache.end());
            state.revert(p);
            assert(state == prev);
            if(outcome == 2) {
                cache.emplace(state, 0);
                return 0;
            } else if(outcome == 0) {
                cache.emplace(state, 2);
                return 2;
            }
        }
    }

    cache.emplace(state, 1);
    return 1;
}

void solveTicTacToeFR(string board[3]) {
    State state = getState(board);
    vector<Position> openPositions = getOpenPositions();
    Position winner;
    Cache cache;
    pair<Position, int> curBest{{}, -1};
    for(const Position& pos : openPositions) {
        state.place(1, pos);
        State prevState = state;
        int prevCacheSize = cache.size();
        int outcome = calculateOutcome(state, cache);
        assert(outcome >= 0 && outcome <= 2);
        assert(prevState == state);
        assert(prevCacheSize + 1 <= cache.size());
        assert(cache.find(state) != cache.end());
        if(outcome == 2) {
            cout << "You win: " << pos << endl;
            return;
        }
        if(outcome > curBest.second) {
            curBest.first = pos;
            curBest.second = outcome;
        }
    }
    if(curBest.second == 1) {
        cout << "Most likely Draw: " << curBest.first << endl;
    } else {
        cout << "You lose: " << curBest.first << endl;
    }
}


int main() {
    string board[3] = {"XOX",
                       ".OX",
                       "O.."};
    solveTicTacToe(board);
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
