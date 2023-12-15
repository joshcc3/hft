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
#include <functional>



using namespace std;

// tODO - always_destroy

using u64 = uint64_t;
using u32 = uint32_t;

#include <iostream>


/*
 *
 *
Model for these continuations
{-# LANGUAGE ExistentialQuantification #-}


data Cont r a = Cont { getExecution :: ((a -> r) ->r) }

(>>==) :: Cont r a -> (a -> Cont r b) -> Cont r b
(>>==) (Cont ca) f = Cont cb
  where
    cb bcont = ca wrappedCont
        where
            wrappedCont a = cb bcont
              where
                Cont cb = f a

main = print (getExecution (c1 >>== f) (\a -> a))
 *
 *
 */


/*
*c1 :: Cont Int Int
c1 = Cont { getExecution = h }
where
h :: (Int -> Int) -> Int
h f = g 10
wheremm
g :: Int -> Int
g x = let a = f x in if a == 0 then 231 else g a

f :: Int -> Cont Int Int
f x = Cont $ \c -> c (x - 1)
*/



int main() {

}