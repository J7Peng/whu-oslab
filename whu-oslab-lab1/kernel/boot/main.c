// #include "riscv.h"
// #include "lib/print.h"
// #include "proc/proc.h"
// #include "dev/uart.h"
// #include "mem/pmem.h"
// #include "mem/vmem.h"
// #include "lib/str.h"

// // volatile int started = 0;

// // void main()
// // {
// //     if(mycpuid()==0){
// //         uart_init();
// //         print_init();
// //         printf("\n Hello os! \n");
// //         __sync_synchronize();
// //         started = 1;
// //     }
// //     else{
// //         while (started == 0);   
// //         __sync_synchronize();
// //         printf("\nhart %d starting\n", (int)mycpuid());
// //     }
// //     while (1);    

    
// // }

// volatile static int started = 0;

// volatile static int over_1 = 0, over_2 = 0;

//static int* mem[1024];

//int main()
// {
//     int cpuid = r_tp();

//     if(cpuid == 0) {

//         print_init();
//         pmem_init();

//         printf("cpu %d is booting!\n", cpuid);
//         __sync_synchronize();
//         started = 1;

//         for(int i = 0; i < 512; i++) {
//             mem[i] = pmem_alloc(true);
//             memset(mem[i], 1, PGSIZE);
//             printf("mem0 = %p, data = %d\n", mem[i], mem[i][0]);
//         }
//         printf("cpu %d alloc over\n", cpuid);
//         over_1 = 1;
        
//         while(over_1 == 0 || over_2 == 0);
        
//         for(int i = 0; i < 512; i++)
//             pmem_free((uint64)mem[i], true);
//         printf("cpu %d free over\n", cpuid);

//     } else {

//         while(started == 0);
//         __sync_synchronize();
//         printf("cpu %d is booting!\n", cpuid);
        
//         for(int i = 512; i < 1024; i++) {
//             mem[i] = pmem_alloc(true);
//             memset(mem[i], 1, PGSIZE);
//             printf("mem1 = %p, data = %d\n", mem[i], mem[i][0]);
//         }
//         printf("cpu %d alloc over\n", cpuid);
//         over_2 = 1;

//         while(over_1 == 0 || over_2 == 0);

//         for(int i = 512; i < 1024; i++)
//             pmem_free((uint64)mem[i], true);
//         printf("cpu %d free over\n", cpuid);        
 
//     }
//     while (1);    
// }
// int main()
// {
//     int cpuid = r_tp();

//     if(cpuid == 0) {

//         print_init();
//         pmem_init();
//         kvm_init();
//         kvm_inithart();

//         printf("cpu %d is booting!\n", cpuid);
//         __sync_synchronize();
//         // started = 1;

//         pgtbl_t test_pgtbl = pmem_alloc(true);
//         uint64 mem[5];
//         for(int i = 0; i < 5; i++)
//             mem[i] = (uint64)pmem_alloc(false);

//         printf("\ntest-1\n\n");    
//         vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
//         vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE / 2, PTE_R | PTE_W);
//         vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE - 1, PTE_R | PTE_X);
//         vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[2], PGSIZE, PTE_R | PTE_X);
//         vm_mappages(test_pgtbl, VA_MAX - PGSIZE, mem[4], PGSIZE, PTE_W);
//         vm_print(test_pgtbl);

//         printf("\ntest-2\n\n");    
//         vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
//         vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, true);
//         vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, true);
//         vm_print(test_pgtbl);

//     } else {

//         while(started == 0);
//         __sync_synchronize();
//         printf("cpu %d is booting!\n", cpuid);
         
//     }
//     while (1);    
// }
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/uart.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
// #include "lib/string.h"
#include "trap/trap.h"
#include "dev/timer.h"

volatile static int started = 0;

int main(void)
{
    int id = mycpuid();   // 或者用 cpuid()/mycpuid()

    if (id == 0) {
        // 基础初始化
        trap_kernel_init();
        print_init();
        uart_init();
        // pmem_init();
        // kvm_init();
        // kvm_inithart();

        // 安装 S-mode trap 入口（全局一次）
       

        // 本核的中断/PLIC/定时器（每核）
        trap_kernel_inithart();   // 内部应打开 SIE_STIE + SSTATUS_SIE
        timer_create();           // 预约首次 stimecmp = time + INTERVAL

        printf("cpu %d is booting! Sstc timer test starts.\n", id);

        __sync_synchronize();
        started = 1;              // 放行其他核

        // 心跳观测循环：每 10 tick 打印一次
        uint64 last = timer_get_ticks();
        while (1) {
            
            uint64 t = timer_get_ticks();
            if (t != last) {
                if ((t % 10) == 0) {     // 假设 INTERVAL=0.1s → 约 1 秒打印
                    printf("[cpu%d] ticks=%d\n", id, t);
                }
                last = t;
            }
        }

    } else {
        // 等待 boot 核完成全局初始化
        while (started == 0) { /* spin */ }
        __sync_synchronize();

        // 每核初始化：打开本核中断 & 预约本核 stimecmp
        trap_kernel_inithart();
        printf("cpu %d is booting! Sstc timer armed.\n", id);

        // 次核也跑一个轻量观测（减少刷屏：每 50 tick 打印一次）
        uint64 last = timer_get_ticks();
        while (1) {
            uint64 t = timer_get_ticks();
            if (t != last) {
                if ((t % 50) == 0) {
                    printf("[cpu%d] ticks=%d\n", id, t);
                }
                last = t;
            }
        }
    }
}