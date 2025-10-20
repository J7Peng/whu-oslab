#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "memlayout.h"
#include "riscv.h"

// 中断信息
static char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
static char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    w_stvec((uint64)kernel_vector); 

    // 全局 PLIC 初始化
    plic_init(); 
}

// 各个核心trap初始化
void trap_kernel_inithart()
{   
    // 打开本核 S-mode timer interrupt
    plic_inithart();         // 外设中断
    w_sie(r_sie() | SIE_STIE); // 开启所有 S-mode 中断
    intr_on();
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    int irq = plic_claim(); // 获取待处理外设中断号

    if (irq == 0) {
        // PLIC 号为0表示没有有效中断，忽略
        return;
    }

    // 处理 UART 中断示例
    if (irq == UART_IRQ) {
        int c = uart_getc_sync();
        if (c != -1) {
            uart_putc_sync(c); // 回显字符
        }
    }

    // 完成处理，通知 PLIC
    plic_complete(irq);
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    
    
    int hart = mycpuid();

    // 系统时钟只由 hart 0 更新
    if (hart == 0) {
        timer_update(); // ticks++
    }
    w_stimecmp(r_time() + INTERVAL);
    // 输出滴答字符用于测试
    
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    //uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    //assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    //assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    int trap_id = scause & 0xf; 

    if (scause & (1UL << 63)) {
        // 中断
        switch(trap_id) {
            case 5: // S-mode timer interrupt
                timer_interrupt_handler();
                break;
            case 9: // S-mode external interrupt
                external_interrupt_handler();
                break;
            default:
                // 其他中断直接panic，并打印trap信息
                if (trap_id < 16)
                    panic(interrupt_info[trap_id]);
                else{
                    printf("unknown trap (stval=0x%llx)\n", stval);
                    panic("interrupt trap");
                }
                break;
        }
    } else {
        printf("exeption!%d\n", trap_id);
        
        // 异常
        if (trap_id < 16)
            panic(exception_info[trap_id]);
        else
            panic("Unknown exception");
    }

    // 返回S-mode指令地址
    w_sepc(sepc);
}