//
// Created by jc on 24/10/23.
//

#ifndef HFT_MYTYPEDEFS_H
#define HFT_MYTYPEDEFS_H

#include <cstdint>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using Qty = i32;
using OrderId = i64;
using PriceL = u64;
using NotionalL = i64;

template<typename T>
T abs(T x) {
    return x >= 0 ? x : -x;
}


#endif //HFT_MYTYPEDEFS_H
