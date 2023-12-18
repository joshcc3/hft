//
// Created by joshuacoutinho on 19/12/23.
//

#ifndef CONTEXTMGR_H
#define CONTEXTMGR_H

#include "defs.h"
#include "../../cppkern/ucontext_.h"


// TODO - where is the best place to allocate the context and the stacks.

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

inline void interruptCtxDebugTrap() {
    assert(false);
}

struct Coroutine {
    u64 handle;
    void (*trampoline)(u64 handle);
};

class ContextMgr {
public:
    constexpr static int STACK_SIZE = 1 << 16;


    ucontext_t blockingRecvCtx{};
    ucontext_t interruptCtx{};

    u8* blockingStack{};

    void init(const Coroutine& blockingRecvCo) {

	// TODO - use compound pages here
    	blockingStack = static_cast<u8*>(malloc(STACK_SIZE));

        assert(blockingStack);


        void (*blockingRecvTrampoline)() = reinterpret_cast<void(*)()>(blockingRecvCo.trampoline);
        getcontext_(&blockingRecvCtx);
        blockingRecvCtx.uc_link = 0;
        blockingRecvCtx.uc_stack.ss_sp = blockingStack;
        blockingRecvCtx.uc_stack.ss_size = STACK_SIZE;
        makecontext_(&blockingRecvCtx, blockingRecvTrampoline, 1, blockingRecvCo.handle);

        // Initialize context2
        getcontext_(&interruptCtx);
        interruptCtx.uc_link = 0;
    }

};



#endif //CONTEXTMGR_H
