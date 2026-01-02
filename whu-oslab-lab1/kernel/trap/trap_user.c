#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"
#include "syscall/syscall.h"
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



void trap_user_handler()
{
    w_stvec((uint64)kernel_vector);

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
            timer_interrupt_handler();   
            if (p != 0 && p->state == RUNNING) {
                proc_yield();
            }
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
        //printf("syscall from user mode\n");
        intr_on(); // 允许中断
        syscall();
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
    //printf("trap_user_return\n");
    proc_t *p = myproc();
    intr_off();

    p->tf->kernel_satp = MAKE_SATP(kernel_pgtbl);//内核页表
    p->tf->kernel_hartid = r_tp();
    p->tf->kernel_sp = p->kstack+PGSIZE; // 内核栈顶
    p->tf->kernel_trap = (uint64)trap_user_handler;

    volatile int64 fn = (uint64)TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    
    w_stvec((uint64)TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));
     //中断相关寄存器设置
    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;
    x |=  SSTATUS_SPIE;
    w_sstatus(x);
    
    w_sepc(p->tf->epc);  

   
   ((void (*)(uint64,uint64))fn)((uint64)TRAPFRAME, MAKE_SATP(p->pgtbl));//调用了user_return
    panic("trap_user_return unreachable");
}
