#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "riscv.h"

#include "fs/fs.h"
#include "fs/dir.h"
#include "fs/inode.h"
#include "fs/file.h"

/*----------------外部空间------------------*/

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();

/*----------------本地变量------------------*/

// 进程数组
#define NPROC 64
static proc_t procs[NPROC];

// 第一个进程的指针
static proc_t *proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;

struct spinlock wait_lock;


extern void trap_user_handler();


extern pgtbl_t kernel_pgtbl;


// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{   
    static int first = 1;
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    if(first)
    {
        first = 0;
        fs_init();
    }
    trap_user_return();
}




// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
     pgtbl_t upgt = (pgtbl_t)pmem_alloc(true);//给用户页表分配一页内存
    if(!upgt)
    {
        panic("proc_pgtbl_init: pmem_alloc failed");
    }
    memset(upgt,0,PGSIZE);
    trapframe = PG_ROUND_DOWN(trapframe);
    //TRAMFRAME是一个虚拟地址，映射到每一个进程的trapframe的物理地址
    vm_mappages(upgt,(uint64)TRAPFRAME,trapframe,PGSIZE,PTE_R | PTE_W|PTE_V);
    //trampoline 映射
    uint64 tramp_pa = (uint64)trampoline;
    vm_mappages(upgt,(uint64)TRAMPOLINE,tramp_pa,PGSIZE,PTE_X | PTE_R|PTE_V);

    return upgt;
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
    struct proc *p;
    p = proc_alloc();
  
    if(!p)
    {
        panic("proc_make_first: proc_alloc failed");
    }
    p->parent = 0; // 第一个进程没有父进程
    proczero = p; // 复制到静态变量中
    // ustack 映射 + 设置 ustack_pages 
    void * ustack_kva = pmem_alloc(false);
    if(!ustack_kva)
    {
        panic("proc_make_first: pmem_alloc for ustack failed");
    }
    memset(ustack_kva,0,PGSIZE);
    int64 ustack_pa = (int64)(ustack_kva);

    int64 USTACK_TOP = TRAPFRAME;
    int64 USTACK_BOTTOM = USTACK_TOP - PGSIZE;
    vm_mappages(p->pgtbl,USTACK_BOTTOM,ustack_pa,PGSIZE,PTE_R | PTE_W | PTE_U);
    p->ustack_pages = 1;
    // data + code 映射
    if(initcode_len>PGSIZE) panic("proc_make_first: initcode too large");
    uint64 page = (uint64)pmem_alloc(false);
    if(!page)
    {
        panic("proc_make_first: pmem_alloc for code+data failed");
    }
    memset((void*)page,0,PGSIZE);
    memcpy((void*)page,initcode,initcode_len);
    uint64 code_pa = (uint64)page;
  
    // 代码虚拟地址放在 0x1000（第一页空洞后）
    uint64 CODE_VA = PGSIZE;
    vm_mappages(p->pgtbl,CODE_VA,code_pa,PGSIZE,PTE_R | PTE_W | PTE_X | PTE_U);
    // 设置 heap_top,
    p->heap_top = CODE_VA + PGSIZE; // 紧接着代码段后面
    // tf字段设置
    p->tf->epc = CODE_VA; // 从代码段开始执行
    p->tf->sp = USTACK_TOP; // 用户栈顶
    p->tf->kernel_satp = MAKE_SATP(kernel_pgtbl);//内核页表
    p->tf->kernel_hartid = r_tp();
    p->tf->kernel_sp = p->kstack+PGSIZE; // 内核栈顶
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->state = RUNNABLE;

    struct cpu *c = mycpu();
    c->proc = p;
    p->cwd = path_to_inode("/");// 设置当前工作目录为根目录
    spinlock_release(&p->lk);

    printf("proczero: pid=%d state=%d tf=%p pgtbl=%p kstack=%p\n",
       proczero->pid, proczero->state, proczero->tf, proczero->pgtbl, proczero->kstack);
}




proc_t* proc_alloc()
{
    proc_t* p;

    for(p = procs;p<&procs[NPROC];p++)
    {
        spinlock_acquire(&p->lk);
        if(p->state == UNUSED)
        {
            goto FOUND;
        }
        else
        {
            spinlock_release(&p->lk);
        }
    }
    return 0;//fail
FOUND:
    p->pid = alloc_pid();

    // trapframe 申请
    void * tf_kva = pmem_alloc(true);
    if(!tf_kva)
    {
        proc_free(p);
        spinlock_release(&p->lk);
        return 0;
    }
    memset(tf_kva,0,PGSIZE);
    p->tf = (trapframe_t*)tf_kva;

    // pgtbl 申请
    pgtbl_t upgt = proc_pgtbl_init((uint64)(tf_kva));
  
    p->pgtbl = upgt;
   
     // 内核字段设置
    void * kstack_pa = pmem_alloc(true);
    if(!kstack_pa)
    {
        panic("proc_alloc: pmem_alloc for kstack failed");
    }
    memset(kstack_pa,0,PGSIZE);
    vm_mappages(kernel_pgtbl,p->kstack,(uint64)kstack_pa,PGSIZE,PTE_R | PTE_W);
    p->tf->kernel_sp = p->kstack+PGSIZE;// 设置内核栈顶
    // 上下文设置
    memset(&p->ctx,0,sizeof(p->ctx));
    p->ctx.ra = (uint64)fork_return;
    p->ctx.sp = p->kstack+PGSIZE; // 内核栈顶

    return p;
}



void proc_free(proc_t* p)
{
    if (p->pgtbl) {
        if (p->heap_top > PGSIZE) {
            uint64 len = p->heap_top - PGSIZE;
            vm_unmappages(p->pgtbl, PGSIZE, len, true);
        }

        if (p->ustack_pages > 0) {
            uint64 stack_bottom =
                TRAPFRAME - (uint64)p->ustack_pages * PGSIZE;
            uint64 stack_len =
                (uint64)p->ustack_pages * PGSIZE;
            vm_unmappages(p->pgtbl, stack_bottom, stack_len, true);
        }

        pmem_free((uint64)p->pgtbl, true);
        p->pgtbl = 0;
    }

    if (p->tf) {
        pmem_free((uint64)p->tf, true);
        p->tf = 0;
    }
    p->pid = 0;
    p->parent = 0;
    p->exit_state = 0;
    p->sleep_space = 0;
    p->heap_top     = 0;
    p->ustack_pages = 0;
    
   
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->state = UNUSED;
}

void proc_init()
{
    struct proc *p;
  
    spinlock_init(&lk_pid, "nextpid");
    spinlock_init(&wait_lock, "wait_lock");
    for(p = procs; p < &procs[NPROC]; p++) {
      spinlock_init(&p->lk, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - procs));
    }
}

static void proc_wakeup_one(proc_t* p)
{
    // printf("proc_wakeup_one: called for pid=%d, caller_holding=%d\n", p->pid, spinlock_holding(&p->lk));
    // printf(" proc_wakeup_one: before: state=%d, sleep_space=%p\n", p->state, p->sleep_space);

    assert(spinlock_holding(&p->lk), "proc_wakeup_one: lock");
    if(p->state == SLEEPING && p->sleep_space == p) {
        p->state = RUNNABLE;
        printf("proc_wakeup_one: pid=%d is now RUNNABLE\n", p->pid);
    } else {
        printf(" proc_wakeup_one: pid=%d not matching sleep_space or not SLEEPING\n", p->pid);
    }
}


int proc_fork()
{
    int  pid;
    struct proc *np;
    struct proc *p = myproc();

    if((np = proc_alloc()) == 0)
    {
        return -1;
    }
  
    if(uvmcopy(p->pgtbl,np->pgtbl,p->heap_top , p->ustack_pages )<0)
    {
        proc_free(np);
        spinlock_release(&np->lk);
        return -1;
    }
    
    *(np->tf) =*(p->tf);//复制trapframe
    np->tf->a0 = 0;//子进程返回值为0

    for(int i = 0;i < FILE_PER_PROC;i++)
    {
        if(p->filelist[i])
        {
            np->filelist[i] = file_dup(p->filelist[i]);
        }
    }
   
    np->cwd = inode_dup(p->cwd);

    pid = np->pid;
    spinlock_release(&np->lk);
    spinlock_acquire(&wait_lock);
    np->parent = p;
    spinlock_release(&wait_lock);
    spinlock_acquire(&np->lk);
    np->state = RUNNABLE;
    np->ustack_pages = p->ustack_pages;
    np->heap_top = p->heap_top;
    spinlock_release(&np->lk);
    printf("pid=%d\n",pid);
    return pid;
}

void proc_yield()
{
    proc_t *p = myproc();

    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched();
    spinlock_release(&p->lk);

}
//////
int proc_wait(uint64 addr)
{
    struct proc *pp;
    int havekids, exit_state;
    struct proc *p = myproc();

    spinlock_acquire(&wait_lock);
    for(;;)
    {
        havekids = 0;
        
        for(pp = procs;pp<&procs[NPROC];pp++)
        {
            if(pp->parent == p)
            {
               
                spinlock_acquire(&pp->lk);
                havekids = 1;
                if(pp->state == ZOMBIE)
                {
                    // found one
                    exit_state = pp->exit_state;
                    if(addr != 0  )
                    {
                        uvm_copyout(p->pgtbl, addr, (uint64)&pp->exit_state, sizeof(pp->exit_state));
                    }
                   
                    proc_free(pp);
                    spinlock_release(&pp->lk);
                    spinlock_release(&wait_lock);
                    return exit_state;
                }
                spinlock_release(&pp->lk);
            }
        }
        if(!havekids )
        {
            spinlock_release(&wait_lock);
            return -1;
        }
        proc_sleep(p, &wait_lock);
    }

}

static void proc_reparent(proc_t* parent)
{
    struct proc *pp;
    for(pp = procs;pp<&procs[NPROC];pp++)
    {
       
        if(pp->parent == parent)
        {
            pp->parent = proczero;
            spinlock_acquire(&proczero->lk);
            proc_wakeup_one(proczero);
            spinlock_release(&proczero->lk);
        }
       
    }
}

void proc_exit(int exit_state)
{
    struct proc *p = myproc();

    if(p==proczero)
        panic("proc_exit: proczero");
    
    //file system related TBD...

    spinlock_acquire(&wait_lock);

    proc_reparent(p);


    spinlock_acquire(&p->lk);
    p->exit_state = exit_state;
    p->state = ZOMBIE;
    spinlock_release(&p->lk);

    if (p->parent) {
        spinlock_acquire(&p->parent->lk);
        proc_wakeup_one(p->parent);
        spinlock_release(&p->parent->lk);
    }

    spinlock_release(&wait_lock);
    intr_off();
    spinlock_acquire(&p->lk);
    
    proc_sched();
    panic("proc_exit: zombie exit");
}

void proc_sched()
{
    int origin;
    proc_t *p = myproc();

    if (!p) panic("proc_sched: no process");

    if(!spinlock_holding(&p->lk))
        panic("proc_sched: p->lk not held");
    if(mycpu()->noff != 1)
        panic("proc_sched: sched locks");
    if(p->state == RUNNING)
        panic("proc_sched: running");
    if(intr_get())
        panic("proc_sched: interruptible");

    // printf("proc_sched: ENTER p=%p pid=%d state=%d sleep_space=%p cpu->proc=%p ctx.ra=%p ctx.sp=%p\n",
    //        (void*)p, p->pid, p->state, p->sleep_space, (void*)mycpu()->proc,
    //        (void*)p->ctx.ra, (void*)p->ctx.sp);

    origin = mycpu()->origin;

    /* 保持我们之前的防护，清理 cpu->proc */
    mycpu()->proc = 0;

    swtch(&p->ctx, &mycpu()->ctx);
    mycpu()->origin = origin;

    // printf("proc_sched: RETURN p=%p pid=%d now cpu->proc=%p ctx.ra=%p ctx.sp=%p\n",
    //        (void*)p, p->pid, (void*)mycpu()->proc,
    //        (void*)p->ctx.ra, (void*)p->ctx.sp);

}



void proc_scheduler()
{
    struct cpu *c = mycpu();
    c->proc = 0;

    for (;;) {
    intr_on();
    for (struct proc *p = procs; p < &procs[NPROC]; p++) {
        spinlock_acquire(&p->lk);
        if (p->state == RUNNABLE) {
            p->state = RUNNING;
            c->proc = p;
            swtch(&c->ctx, &p->ctx);
            c->proc = 0;
        }
        spinlock_release(&p->lk);
    }
}

}


// 进程睡眠在sleep_space
void proc_sleep(void* sleep_space, spinlock_t* lk)
{
    proc_t *p = myproc();

    spinlock_acquire(&p->lk);
    spinlock_release(lk);

    p->sleep_space = sleep_space;
    p->state = SLEEPING;
    
    proc_sched();

    p->sleep_space = 0;
    spinlock_release(&p->lk);
    spinlock_acquire(lk);
}

// 唤醒所有在sleep_space沉睡的进程
void proc_wakeup(void* sleep_space)
{
    struct proc *p;
    for(p = procs; p < &procs[NPROC]; p++) {
       if(p!=myproc()) {
            spinlock_acquire(&p->lk);
            if(p->state == SLEEPING && p->sleep_space == sleep_space) {
                p->state = RUNNABLE;
            }
            spinlock_release(&p->lk);
       }
    }
}

void 
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    proc_t *p = myproc();
    if(user_dst) {
        uvm_copyout(p->pgtbl, dst, (uint64)src, len);
    } else {
        memmove((void *)dst, src, len);
    }
}