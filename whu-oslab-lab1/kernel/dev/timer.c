#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"
#include"proc/proc.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5]__attribute__((used));

// 时钟初始化
// called in start.c
void timer_init()
{
    w_menvcfg(r_menvcfg() | (1L<<63)); // 允许S模式下写 stimecmp
    w_mcounteren(r_mcounteren() | 2);  // S态能读 time
    w_stimecmp(r_time() + INTERVAL);

}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    int hart = mycpuid();
    if (hart == 0)
    {
        sys_timer.ticks = 0;
        spinlock_init(&sys_timer.lk, "timer");
        w_stimecmp(r_time() + INTERVAL);
    }
    // 允许 S-mode 全局中断
    w_sstatus(r_sstatus() | SSTATUS_SIE);
    printf("hart%d timer create \n",mycpuid());
}


// 时钟更新(ticks++ with lock)
void timer_update()
{
    int hart = mycpuid();
    if (hart != 0) return; // 非 hart0 不更新 ticks

    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);

}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    spinlock_acquire(&sys_timer.lk);
    uint64 t = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    return t;
}