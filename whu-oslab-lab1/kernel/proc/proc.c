#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();


// 第一个进程
static proc_t proczero;

extern void trap_user_handler();

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe_kva)
{
    // 1. 分配页表根页
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(false);
    if (!pgtbl)
        panic("proc_pgtbl_init: pmem_alloc failed");
    memset(pgtbl, 0, PGSIZE);

    // 2. trampoline 页映射（用户态入口 trampoline，高地址）
    uint64 tramp_va = TRAMPOLINE;       // 用户虚拟地址空间最高页
    uint64 tramp_pa = (uint64)trampoline;   // trampoline 内核虚拟地址
    vm_mappages(pgtbl, tramp_va, tramp_pa, PGSIZE, PTE_R | PTE_X);

    // 3. trapframe 页映射（trampoline 下方一页，用户可访问）

    uint64 tf_va = tramp_va - PGSIZE;
    vm_mappages(pgtbl, tf_va, trapframe_kva, PGSIZE, PTE_R | PTE_W);

    // 4. (可选) 这里不映射用户栈和代码，交由 proc_make_first() 映射
    //    但如果想在这里映射初始用户栈，也可以：
    // uint64 ustack_va = USER_STACK_BOTTOM; // 定义你的用户栈虚拟地址
    // uint64 ustack_pa = (uint64)pmem_alloc(false);
    // vm_mappages(pgtbl, ustack_va, ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_first()
{
    //uint64 page;
    
    // pid 设置

    // pagetable 初始化

    // ustack 映射 + 设置 ustack_pages 

    // data + code 映射
    //assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");

    // 设置 heap_top

    // tf字段设置

    // 内核字段设置

    // 上下文切换

    proc_t *p = &proczero;
    memset(p, 0, sizeof(proc_t));

    p->pid = 1;

    // 分配内核栈和 trapframe
    p->kstack = (uint64)pmem_alloc(true);
    p->tf = (trapframe_t*)pmem_alloc(true);

    // 初始化页表（传入 trapframe 地址）
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);

    //映射用户 code/data
    uint64 uva_code = 0x1000; // 跳过最低页
    uint64 pa_code = (uint64)pmem_alloc(false);
    memset((void*)pa_code, 0, PGSIZE);
    memmove((void*)pa_code, initcode, initcode_len);
//for test
unsigned char *code_kva = (unsigned char*)pa_code;
printf("initcode bytes:");
for (int i = 0; i < 16 && i < initcode_len; ++i)
    printf(" %x", (int)code_kva[i]);
printf("\n");
printf("initcode_len=%d\n", (int)initcode_len);
//end

    vm_mappages(p->pgtbl, uva_code, pa_code, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    // 映射用户栈
    uint64 uva_stack = uva_code + 2 * PGSIZE;
    uint64 pa_stack = (uint64)pmem_alloc(false);
    vm_mappages(p->pgtbl, uva_stack, pa_stack, PGSIZE, PTE_R | PTE_W | PTE_U);
    p->ustack_pages = 1;

    // 设置堆顶
    p->heap_top = uva_code + PGSIZE;

    // 初始化 trapframe
    memset(p->tf, 0, sizeof(trapframe_t));
    p->tf->epc = uva_code;                 // 用户程序入口
    p->tf->sp = uva_stack + PGSIZE;        // 用户栈顶

    // 内核辅助字段
    p->tf->kernel_satp = r_satp();
    p->tf->kernel_sp = p->kstack + PGSIZE;
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = mycpuid();

    // 初始化 context（用于 swtch）
    memset(&p->ctx, 0, sizeof(context_t));
    p->ctx.ra = (uint64)trap_user_return;
    p->ctx.sp = p->kstack + PGSIZE;

    // 将 CPU 当前进程设置为 p
    cpu_t *c = mycpu();
    c->proc = p;

    printf("proc_make_first: switching to user process...\n");

    // 进行上下文切换
    swtch(&c->ctx, &p->ctx);

    // 永不返回
    panic("proc_make_first: should never return");
}