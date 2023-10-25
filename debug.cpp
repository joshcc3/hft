
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

struct A {

    int x = 0;

    template<bool IsConst = false>
    auto getValue() -> std::conditional_t<IsConst, const int &, int &> {
        return x;
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

int main() {

    A a;
    int& x = a.getValue();
    const int& y = a.getValue();

    ++x;
    cout << y << endl;

}
