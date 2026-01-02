#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/uart.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/str.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "proc/cpu.h"
#include "dev/plic.h"
#include "fs/fs.h"
#include "dev/vio.h"
#include "fs/file.h"
#include "fs/inode.h"
#include "fs/buf.h"
volatile static int started = 0;

int main(void)
{
    int id = mycpuid();   // 或者用 cpuid()/mycpuid()

    if (id == 0) {
        // 基础初始化
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        print_init();
        uart_init();
        proc_init();
        buf_init();
        inode_init();
        file_init();
        console_init();
        // 安装 S-mode trap 入口（全局一次）

        // 本核的中断/PLIC/定时器（每核）
        trap_kernel_inithart();   // 内部应打开 SIE_STIE + SSTATUS_SIE

       
        printf("cpu %d is booting! \n", id);

        __sync_synchronize();
        started = 1;              // 放行其他核

         
        proc_make_first();    
        virtio_disk_init();   
        proc_scheduler(); 
        
        
    } else {
        // 等待 boot 核完成全局初始化
        while (started == 0) { /* spin */ }
        __sync_synchronize();
        kvm_inithart();
        // 每核初始化：打开本核中断 & 预约本核 stimecmp
        trap_kernel_inithart();
        
        printf("cpu %d is booting! Sstc timer armed.\n", id);

    }
}