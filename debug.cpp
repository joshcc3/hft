    //
// Created by jc on 25/10/23.
//



using namespace std;




// tODO - always_destroy


#include <iostream>

template <typename X, typename Y>
union A {
    X a;
    Y b;
};

template
void f(A<int, char>& a, int c) {
    cout << a.a + c << endl;
}

int main() {
    A a{.a = 1};
    void (*g)(A&, int) = f;
    g(a, 10);
}

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


