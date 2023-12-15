//
// Created by joshuacoutinho on 17/12/23.
//

#ifndef UCONTEXT__H
#define UCONTEXT__H


#define SIG_BLOCK	0
#define SIG_SETMASK	2
/* Since we cannot include a header to define _NSIG/8, we define it
   here.  */
#define _NSIG8		8
/* Offsets of the fields in the ucontext_t structure.  */
#define oRBP		120
#define oRSP		160
#define oRBX		128
#define oR8		    40
#define oR9		    48
#define oR12		72
#define oR13		80
#define oR14		88
#define oR15		96
#define oRDI		104
#define oRSI		112
#define oRDX		136
#define oRAX		144
#define oRCX		152
#define oRIP		168
#define oFPREGS		208
#define oSIGMASK	280
#define oFPREGSMEM	408
#define oMXCSR		432

#ifdef SHSTK_ENABLED
#error "Shadow stack is enabled"
#endif

#define ASM_SIZE_DIRECTIVE(name) .size name,.-name;

#define END(name)  ASM_SIZE_DIRECTIVE(name)

typedef long long int greg_t;

#define __NGREG	23

struct _libc_fpxreg {
    unsigned short int significand[4];
    unsigned short int exponent;
    unsigned short int __glibc_reserved1[3];
};

struct _libc_xmmreg {
    u32 element[4];
};

struct _libc_fpstate {
    /* 64-bit FXSAVE format.  */
    u16 cwd;
    u16 swd;
    u16 ftw;
    u16 fop;
    u64 rip;
    u64 rdp;
    u32 mxcsr;
    u32 mxcr_mask;
    struct _libc_fpxreg _st[8];
    struct _libc_xmmreg _xmm[16];
    u32 __glibc_reserved1[24];
};

typedef greg_t gregset_t[__NGREG];
typedef struct _libc_fpstate* fpregset_t;


typedef struct {
    void* ss_sp;
    int ss_flags;
    size_t ss_size;
} stack_t;

typedef struct {
    gregset_t gregs;
    /* Note that fpregs is a pointer.  */
    fpregset_t fpregs;
    unsigned long long __reserved1[8];
} mcontext_t;

#define _SIGSET_NWORDS (1024 / (8 * sizeof (unsigned long int)))

typedef struct {
    unsigned long int __val[_SIGSET_NWORDS];
} __sigset_t;

typedef __sigset_t sigset_t;


typedef struct ucontext_t {
    unsigned long int uc_flags;
    struct ucontext_t* uc_link;
    stack_t uc_stack;
    mcontext_t uc_mcontext;
    sigset_t uc_sigmask;
    struct _libc_fpstate __fpregs_mem;
    unsigned long int __ssp[4];
} ucontext_t;

enum
{
    REG_R8 = 0,
  # define REG_R8		REG_R8
    REG_R9,
  # define REG_R9		REG_R9
    REG_R10,
  # define REG_R10	REG_R10
    REG_R11,
  # define REG_R11	REG_R11
    REG_R12,
  # define REG_R12	REG_R12
    REG_R13,
  # define REG_R13	REG_R13
    REG_R14,
  # define REG_R14	REG_R14
    REG_R15,
  # define REG_R15	REG_R15
    REG_RDI,
  # define REG_RDI	REG_RDI
    REG_RSI,
  # define REG_RSI	REG_RSI
    REG_RBP,
  # define REG_RBP	REG_RBP
    REG_RBX,
  # define REG_RBX	REG_RBX
    REG_RDX,
  # define REG_RDX	REG_RDX
    REG_RAX,
  # define REG_RAX	REG_RAX
    REG_RCX,
  # define REG_RCX	REG_RCX
    REG_RSP,
  # define REG_RSP	REG_RSP
    REG_RIP,
  # define REG_RIP	REG_RIP
    REG_EFL,
  # define REG_EFL	REG_EFL
    REG_CSGSFS,		/* Actually short cs, gs, fs, __pad0.  */
  # define REG_CSGSFS	REG_CSGSFS
    REG_ERR,
  # define REG_ERR	REG_ERR
    REG_TRAPNO,
  # define REG_TRAPNO	REG_TRAPNO
    REG_OLDMASK,
  # define REG_OLDMASK	REG_OLDMASK
    REG_CR2
  # define REG_CR2	REG_CR2
  };

extern "C" void makecontext_(ucontext_t* ucp, void (*func)(), int argc, ...);

typedef __builtin_va_list va_list;
#define va_start(v, l)	__builtin_va_start(v, l)
#define va_end(v)	__builtin_va_end(v)
#define va_arg(v, T)	__builtin_va_arg(v, T)
#define va_copy(d, s)	__builtin_va_copy(d, s)

extern "C" void start_context_();
// extern void push_start_context_(ucontext_t*) ;

// Remember when using this library to only call from the same process
// virtual memory and page tables are not refreshed.
// Good question - do kernel threads all share the same page table space?
// when a kernel switches between different threads, what is different?
// whats special about the interrupt handling context?

extern "C" void getcontext_(ucontext_t* o);


extern "C" void setcontext_(const ucontext_t* __ucp);

extern "C" void swapcontext_(ucontext_t* o, ucontext_t* d);

#endif //UCONTEXT__H
