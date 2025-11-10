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
    //trapframe_t* tf = p->tf;

    // 必须确认来自用户模式
    assert((r_sstatus() & SSTATUS_SPP) == 0, "not from user mode");
// printf("<<< trap_user_handler ENTER: pid=%d scause=%p sepc=%p a7=%d a0=%d >>>\n",
//            p ? p->pid : -1, scause, sepc, tf->a7, tf->a0);

    uint64 trap_id = scause & 0xf; 
    if((scause>>63) & 1)
    {
        switch (trap_id)
        {
        case 5:
            timer_interrupt_handler();    // 里面会续期: stimecmp = time + INTERVAL
            trap_user_return();
            
        case 9:
            external_interrupt_handler();
            trap_user_return();
            
        default:
            trap_user_return();
        }
    }

    // 其他异常处理
    switch (trap_id)
    {
    case 8:// syscall
        // 先更新pc，防止重复执行syscall指令
        p->tf->epc = sepc + 4;
        printf("syscall from user mode\n");
        //syscall();
        //intr_on(); // 允许中断
        trap_user_return();
       
    default:
        trap_user_return();
          
    }





    // 其他异常当作错误
    printf("unexpected user trap scause=%p stval=%d mtvec:%p\n", scause, r_stval());
    panic("trap_user_handler");
}





void trap_user_return()
{
    proc_t *p = myproc();
    if (!p) panic("trap_user_return: no current proc");
    // trapframe_t *tf = p->tf;

    // // 填写 kernel 字段（内核仍然访问 tf 的 KVA）
    // tf->kernel_satp = r_satp();
    // tf->kernel_sp = p->kstack + PGSIZE;
    // tf->kernel_trap = (uint64)trap_user_handler;
    // tf->kernel_hartid = mycpuid();

    
    uint64 user_satp = MAKE_SATP((uint64)KVA2PA(p->pgtbl));


    void (*user_ret_fn)(trapframe_t*, uint64) =
        (void(*)(trapframe_t*, uint64))(TRAMPOLINE + user_return - trampoline);

   
    // printf("TUR: pid=%d pgtbl_kva=%p pgtbl_pa=%p user_satp=0x%p tf_kva=%p tf_uva=%p epc=%p sp=%p kstack=%p\n",
    //        p->pid,
    //        (void*)p->pgtbl,
    //        (void*)KVA2PA(p->pgtbl),
    //        (unsigned long)user_satp,
    //        (void*)tf,
    //        (void*)TRAPFRAME,
    //        (void*)tf->epc,
    //        (void*)tf->sp,
    //        (void*)p->kstack);

    // printf("TUR: trampoline=%p user_return=%p calling user_return(u_tf=%p, user_satp=0x%p)...\n",
    //        (void*)trampoline, (void*)user_return, (void*)TRAPFRAME, (unsigned long)user_satp);

    uint64 trampoline_stvec = TRAMPOLINE + (user_vector - trampoline);
    w_stvec(trampoline_stvec);    
    w_sepc(p->tf->epc);

    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;
    x |= SSTATUS_SPIE;
    w_sstatus(x);

    user_ret_fn((trapframe_t*)TRAPFRAME, user_satp);

    panic("trap_user_return: user_return returned");
}
