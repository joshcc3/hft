//
// Created by joshuacoutinho on 17/12/23.
//

#include "coroutine.h"
#include <stdio.h>
#ifdef USEJMP
#include <setjmp.h>
#endif
#include <stdnoreturn.h>

#include "defs.h"
#include "ucontext_.h"

using namespace std;

#ifdef USEJMP
jmp_buf my_jump_buffer;

chrono::time_point<chrono::system_clock> _s;
chrono::time_point<chrono::system_clock> _e;

[[noreturn]] void foo(int status) {
    _s = std::chrono::system_clock::now();
    longjmp(my_jump_buffer, status + 1); // will return status+1 out of setjmp
}

constexpr static int LIMIT = 100;

int main() {
    volatile int cnt = 0; // modified local vars in setjmp scope must be volatile
    volatile bool b;
    _s = std::chrono::system_clock::now();
    b = setjmp(my_jump_buffer) != LIMIT;
    _e = std::chrono::system_clock::now();
    double timeTaken = elapsed(_s, _e);
    cout << "Elapsed: " << (timeTaken * 1e9) << "ns" << endl;
    GET_PC(0) += timeTaken;
    if (b) {
        foo(++cnt);
    }
    double ts = GET_PC(0);
    cout << "Time: " << (ts) * 1e9 / LIMIT << "ns" << endl;
}
#endif


inline void
makecontext_(ucontext_t* ucp, void (*func)(), int argc, ...) {

    assert(argc == 1);
    assert(ucp != nullptr && func != nullptr);

    /* Generate room on stack for parameter if needed and uc_link.  */
    auto* sp = reinterpret_cast<greg_t *>((uintptr_t) ucp->uc_stack.ss_sp
                                            + ucp->uc_stack.ss_size);
    sp -= 1;
    /* Align stack and make space for trampoline address.  */
    sp = reinterpret_cast<greg_t *>((reinterpret_cast<uintptr_t>(sp) & -16L) - 8);
    const unsigned int idx_uc_link = 1;
    /* Setup context ucp.  */
    /* Address to jump to.  */
    ucp->uc_mcontext.gregs[REG_RIP] = reinterpret_cast<uintptr_t>(func);
    /* Setup rbx.*/
    ucp->uc_mcontext.gregs[REG_RBX] = reinterpret_cast<uintptr_t>(&sp[idx_uc_link]);
    ucp->uc_mcontext.gregs[REG_RSP] = reinterpret_cast<uintptr_t>(sp);
    /* Setup stack.  */
    sp[0] = reinterpret_cast<uintptr_t>(&start_context_);
    sp[idx_uc_link] = reinterpret_cast<uintptr_t>(ucp->uc_link);
    va_list ap;
    va_start(ap, argc);
    ucp->uc_mcontext.gregs[REG_RDI] = va_arg(ap, greg_t);
    va_end(ap);
}



#define STACK_SIZE (1 << 16)
#define LIMIT 1'000'000

ucontext_t blockingRecvCtx, mainCtx, epilogueCtx;
char blockingStack[STACK_SIZE];
char mainStack[STACK_SIZE];
char epilogueStack[STACK_SIZE];

auto _s = std::chrono::system_clock::now(); \
auto _e = std::chrono::system_clock::now(); \

chrono::time_point<chrono::system_clock> st;
chrono::time_point<chrono::system_clock> en;


struct IO {
    void recv() {
        st = std::chrono::system_clock::now();
        swapcontext_(&blockingRecvCtx, &mainCtx);
    }

    void interrupt(int i) {
        st = std::chrono::system_clock::now();
        swapcontext_(&mainCtx, &blockingRecvCtx);
    }
};

struct Strategy {
    IO& io;

    Strategy(IO& io): io{io} {
    }

    bool init = true;
    volatile int preparedData = 1;

    void func() {
        do {
            io.recv();
            en = std::chrono::system_clock::now();
            GET_PC(1) += elapsed(st, en);
            preparedData++;
        } while (true);
    }
};

void interruptHandler(u64 ioAddr) {
    en = std::chrono::system_clock::now(); \
    GET_PC(0) += elapsed(st, en);
    auto io = reinterpret_cast<IO *>(ioAddr);
    int i = 0;
    while (i < LIMIT) {
        io->interrupt(i);
        en = std::chrono::system_clock::now(); \
        GET_PC(0) += elapsed(st, en);
        ++i;
    }
}

void strategytrampoline(u64 stratAddr) {
    auto s = reinterpret_cast<Strategy *>(stratAddr);
    s->func();
}

void epilogue() {
    printf("block->interrupt [%f ns]\n", GET_PC(0) * 1'000'000'000.0 / LIMIT);
    printf("interrupt->block [%f ns]\n", GET_PC(1) * 1'000'000'000.0 / LIMIT);

    printf("Main: Program completed\n");
}

int main() {
    // Initialize context1
    IO io;
    Strategy strategy{io};

    getcontext_(&epilogueCtx);
    epilogueCtx.uc_link = 0;
    epilogueCtx.uc_stack.ss_sp = epilogueStack;
    epilogueCtx.uc_stack.ss_size = sizeof(epilogueStack);
    makecontext_(&epilogueCtx, epilogue, 0);

    u64 strategyAddr = reinterpret_cast<u64>(&strategy);
    void (*bFunc)() = reinterpret_cast<void (*)()>(strategytrampoline);
    getcontext_(&blockingRecvCtx);
    blockingRecvCtx.uc_link = 0;
    blockingRecvCtx.uc_stack.ss_sp = blockingStack;
    blockingRecvCtx.uc_stack.ss_size = sizeof(blockingStack);
    makecontext_(&blockingRecvCtx, bFunc, 1, strategyAddr);

    // Initialize context2
    u64 ioAddr = reinterpret_cast<u64>(&io);
    void (*iFunc)() = reinterpret_cast<void (*)()>(interruptHandler);
    getcontext_(&mainCtx);
    mainCtx.uc_link = &epilogueCtx;
    mainCtx.uc_stack.ss_sp = mainStack;
    mainCtx.uc_stack.ss_size = sizeof(mainStack);
    makecontext_(&mainCtx, iFunc, 1, ioAddr);


    // Start execution of function1
    setcontext_(&blockingRecvCtx);

    assert(false);


}
