//
// Created by joshuacoutinho on 17/12/23.
//

#include "ucontext_.h"


// TODO - clear the floating point registers here on entry and exit. - although the compiler might use xmm registers they are assumed to be cleared on function entry and exit as per the system v abi

/*
extern "C" void getcontext_(ucontext_t* o);
extern "C" void setcontext_(const ucontext_t *__ucp);
extern "C" void swapcontext_(ucontext_t* o, ucontext_t* d);
*/

.globl start_context_;
.type start_context_,@function;
.align 16;
start_context_:
	/* This removes the parameters passed to the function given to
	   'makecontext' from the stack.  RBX contains the address
	   on the stack pointer for the next context.  */
	movq	%rbx, %rsp
	/* Don't use pop here so that stack is aligned to 16 bytes.  */
	movq	(%rsp), %rdi		/* This is the next context.  */
	testq	%rdi, %rdi
	je	2f			/* If it is zero exit.  */
	call	setcontext_
	/* If this returns (which can happen if the syscall fails) we'll
	   exit the program with the return error value (-1).  */
	movq	%rax,%rdi
2:

	call	exit
	/* The 'exit' call should never return.  In case it does cause
	   the process to terminate.  */
	hlt


.globl setcontext_;
.type setcontext_,@function;
.align 16;
setcontext_:
	movq %rdi, %rdx

	movq	160(%rdx), %rsp
	movq	128(%rdx), %rbx
	movq	120(%rdx), %rbp
	movq	72(%rdx), %r12
	movq	80(%rdx), %r13
	movq	88(%rdx), %r14
	movq	96(%rdx), %r15

	/* The following ret should return to the address set with
	getcontext.  Therefore push the address on the stack.  */
	movq	168(%rdx), %rcx
	pushq	%rcx
	movq	112(%rdx), %rsi
	movq	104(%rdx), %rdi
	movq	152(%rdx), %rcx
	movq	40(%rdx), %r8
	movq	48(%rdx), %r9
	/* Setup finally %rdx.  */
	movq	136(%rdx), %rdx
	/* End FDE here, we fall into another context.  */
	/* Clear rax to indicate success.  */
	xorl	%eax, %eax
	ret



.globl getcontext_;
.type getcontext_,@function;
.align 16;
getcontext_:

	/* Save the preserved registers, the registers used for passing
	   args, and the return address.  */
	movq	%rbx, 128(%rdi)
	movq	%rbp, 120(%rdi)
	movq	%r12, 72(%rdi)
	movq	%r13, 80(%rdi)
	movq	%r14, 88(%rdi)
	movq	%r15, 96(%rdi)
	movq	%rdi, 104(%rdi)
	movq	%rsi, 112(%rdi)
	movq	%rdx, 136(%rdi)
	movq	%rcx, 152(%rdi)
	movq	%r8, 40(%rdi)
	movq	%r9, 48(%rdi)
	movq	(%rsp), %rcx
	movq	%rcx, 168(%rdi)
	leaq	8(%rsp), %rcx		/* Exclude the return address.  */
	movq	%rcx, 160(%rdi)

	xorl	%eax, %eax
	ret

.globl swapcontext_;
.type swapcontext_,@function;
.align 16;
swapcontext_:
	movq	%rbx, 128(%rdi)
	movq	%rbp, 120(%rdi)
	movq	%r12, 72(%rdi)
	movq	%r13, 80(%rdi)
	movq	%r14, 88(%rdi)
	movq	%r15, 96(%rdi)
	movq	%rdi, 104(%rdi)
	movq	%rsi, 112(%rdi)
	movq	%rdx, 136(%rdi)
	movq	%rcx, 152(%rdi)
	movq	%r8, 40(%rdi)
	movq	%r9, 48(%rdi)
	movq	(%rsp), %rcx
	movq	%rcx, 168(%rdi)
	leaq	8(%rsp), %rcx		/* Exclude the return address.  */
	movq	%rcx, 160(%rdi)
	/* The syscall destroys some registers, save them.  */
	/* Load the new stack pointer and the preserved registers.  */
	movq	160(%rsi), %rsp
	movq	128(%rsi), %rbx
	movq	120(%rsi), %rbp
	movq	72(%rsi), %r12
	movq	80(%rsi), %r13
	movq	88(%rsi), %r14
	movq	96(%rsi), %r15


	/* The following ret should return to the address set with
	getcontext.  Therefore push the address on the stack.  */
	movq	168(%rsi), %rcx
	pushq	%rcx
	/* Setup registers used for passing args.  */
	movq	104(%rsi), %rdi
	movq	136(%rsi), %rdx
	movq	152(%rsi), %rcx
	movq	40(%rsi), %r8
	movq	48(%rsi), %r9
	/* Setup finally %rsi.  */
	movq	112(%rsi), %rsi
	/* Clear rax to indicate success.  */
	xorl	%eax, %eax
	ret
