#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/uart.h"

volatile int started = 0;

void main()
{
    
    if(mycpuid()==0){
        uart_init();
        print_init();
        printf("\n Hello os! \n");
        __sync_synchronize();
        started = 1;
    }

    else{
        while (started == 0);
        uart_init();
        print_init();
        printf("\nhart %d starting\n", (int)mycpuid());
    }

    while (1);    

    
}