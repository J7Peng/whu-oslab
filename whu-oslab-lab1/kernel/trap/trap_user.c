#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
//for test
extern pgtbl_t kernel_pgtbl;
long syscall_dispatch(long n, long a0, long a1, long a2)
{
    switch(n) {
    case 0:
        printf("[sys_print] hello from user!\n");
        return 0;
    default:
        printf("unknown syscall %ld\n", n);
        return -1;
    }
}


void trap_user_handler()
{

    uint64 scause = r_scause();
    uint64 sepc = r_sepc();
    proc_t* p = myproc();
    trapframe_t* tf = p->tf;

    // 必须确认来自用户模式
    assert((r_sstatus() & SSTATUS_SPP) == 0, "not from user mode");
printf("<<< trap_user_handler ENTER: pid=%d scause=0x%p sepc=0x%p a7=%d a0=%d >>>\n",
           p ? p->pid : -1, scause, sepc, tf->a7, tf->a0);
    if ((scause & 0xff) == 8) { // ecall from U-mode
        long sysnum = tf->a7;
        long arg0   = tf->a0;
        long arg1   = tf->a1;
        long arg2   = tf->a2;

        long retval = syscall_dispatch(sysnum, arg0, arg1, arg2);

        tf->a0 = retval;    // return value to user
        w_sepc(sepc + 4);   // skip ecall

        return trap_user_return();
    }

    // 其他异常当作错误
    printf("unexpected user trap scause=%d stval=%d\n", scause, r_stval());
    panic("trap_user_handler");
}

//for test
// static void dump_va_info(pgtbl_t pgtbl, uint64 va, const char *msg) {
//     pte_t *pte = vm_getpte(pgtbl, va, false);
//     if (!pte) {
//         printf("%s: no pte for va %p\n", msg, (void*)va);
//         return;
//     }
//     uint64 ptev = *pte;
//     printf("%s: pte=%p PA=%p flags=%x (V=%d R=%d W=%d X=%d U=%d)\n",
//            msg, (void*)ptev, (void*)PTE_TO_PA(ptev), PTE_FLAGS(ptev),
//            !!(ptev & PTE_V), !!(ptev & PTE_R), !!(ptev & PTE_W),
//            !!(ptev & PTE_X), !!(ptev & PTE_U));
// }

// 调用user_return()
// 内核态返回用户态
// 调用user_return()
// 内核态返回用户态
// 放在 trap_user.c 中，替换你原来的 trap_user_return()
// helper: convert kernel virtual addr to physical (adjust KERNBASE if needed)
// helper: kernel KVA <-> PA conversion for this project



// trap_user.c 的 trap_user_return() —— 替换现有实现
void trap_user_return()
{
    proc_t *p = myproc();
    if (!p) panic("trap_user_return: no current proc");
    trapframe_t *tf = p->tf;

    // 填写 kernel 字段（内核仍然访问 tf 的 KVA）
    tf->kernel_satp = r_satp();
    tf->kernel_sp = p->kstack + PGSIZE;
    tf->kernel_trap = (uint64)trap_user_handler;
    tf->kernel_hartid = mycpuid();

    // user page table satp: 注意 p->pgtbl 是 KVA，需要转换为 PA
    uint64 user_satp = MAKE_SATP((uint64)KVA2PA(p->pgtbl)); // 确认你有 KVA2PA 宏

    // 用户态 trampoline 虚地址和 trapframe 的用户虚拟地址
    uint64 tramp_va = MAXVA - PGSIZE;
    uint64 tf_va = tramp_va - PGSIZE;



    void (*user_ret_fn)(trapframe_t*, uint64) =
        (void(*)(trapframe_t*, uint64))(TRAMPOLINE + user_return - trampoline);

    // debug 打印（确保使用 %p / %lx 打 64-bit）
    printf("TUR: pid=%d pgtbl_kva=%p pgtbl_pa=%p user_satp=0x%p tf_kva=%p tf_uva=%p epc=%p sp=%p kstack=%p\n",
           p->pid,
           (void*)p->pgtbl,
           (void*)KVA2PA(p->pgtbl),
           (unsigned long)user_satp,
           (void*)tf,
           (void*)tf_va,
           (void*)tf->epc,
           (void*)tf->sp,
           (void*)p->kstack);

    printf("TUR: trampoline=%p user_return=%p calling user_return(u_tf=%p, user_satp=0x%p)...\n",
           (void*)trampoline, (void*)user_return, (void*)tf_va, (unsigned long)user_satp);

    uint64 trampoline_stvec = TRAMPOLINE + (user_vector - trampoline);
    w_stvec(trampoline_stvec);    
    
    user_ret_fn((trapframe_t*)tf_va, user_satp);

    panic("trap_user_return: user_return returned");
}
