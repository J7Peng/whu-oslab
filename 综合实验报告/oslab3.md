# 中断处理与时钟管理实验报告



## 1. 实验目的

- 理解 RISC-V 多核 CPU 启动流程及 hart 概念。
- 掌握 S-mode trap 的配置与中断处理流程。
- 实现基于 timer interrupt 的多核时钟管理系统。
- 实现 UART 中断处理，实现键盘输入响应。
- 通过 QEMU 模拟验证中断系统和 timer tick 功能。

------

## 2. 实验原理

### 2.1 多核 hart

- 每个 hart 独立运行，hart0 负责全局初始化（trap vector、timer 等），其他 hart 等待启动信号。

### 2.2 S-mode Trap

- S-mode trap 用于处理 Supervisor 模式下的异常或中断。
- 入口地址通过 `stvec` 寄存器设置。
- trap handler 分为：
  - Timer interrupt（S-mode timer interrupt）
  - External interrupt（S-mode external interrupt, 包含 UART）
  - 异常处理（Instruction/Load/Store 等）

### 2.3 Timer 中断

- 每个 hart 配置独立的 `stimecmp` 寄存器。
- hart0 在中断处理函数中更新系统 tick。
- hart1 等其他 hart 可读取全局 tick 进行观测或调度。

### 2.4 UART 中断

- UART 外设通过 PLIC 触发 S-mode external interrupt。
- 中断发生时，读取 UART RX FIFO，并通过 UART TX 回显字符。
- 通过 S-mode trap 可以在多核环境下可靠处理 UART 输入。

------

## 3. 实验环境

- **硬件模拟**：QEMU RISC-V 64-bit, SMP 2 cores
- **内核代码**：自实现 S-mode trap + timer + UART 模块
- **工具链**：riscv64-unknown-elf-gcc / riscv64-unknown-elf-ld
- **调试方式**：通过 QEMU `-nographic` 终端观察 UART 输出

------

## 4. 实验步骤

### 4.1 hart 启动与初始化

```
int id = mycpuid();
if (id == 0) {
    trap_kernel_init();      // trap vector, PLIC 初始化
    print_init();            // UART 打印初始化
    uart_init();             // UART 初始化
    trap_kernel_inithart();  // hart0 中断初始化
    timer_create();          // 初始化系统定时器
    started = 1;             // 放行其他 hart
} else {
    while (started == 0) { /* spin */ }
    __sync_synchronize();
    trap_kernel_inithart();  // 次核初始化
    timer_create();          // 初始化本核 stimecmp
}
```

### 4.2 S-mode trap 配置

```
void trap_kernel_inithart() {
    plic_inithart();                  // 外设中断初始化
    w_sie(r_sie() | SIE_STIE);       // 开启 S-mode timer interrupt
    intr_on();                        // 全局开中断
}
```

### 4.3 Timer 创建与中断处理

```
void timer_create() {
    if(mycpuid()==0) sys_timer.ticks = 0;
    w_stimecmp(r_time() + INTERVAL);     
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

void timer_interrupt_handler() {
    if(mycpuid() == 0) sys_timer.ticks++;  // hart0 更新系统 tick
    w_stimecmp(r_time() + INTERVAL);       // 预约下一次中断
}
```

### 4.4 UART 中断处理

```
void external_interrupt_handler() {
    int irq = plic_claim();
    if(irq == UART_IRQ) {
        int c = uart_getc_sync();
        if(c != -1) uart_putc_sync(c); // 回显字符
    }
    plic_complete(irq);
}
```

### 4.5 主循环打印系统 tick

```
uint64 last = timer_get_ticks();
while(1) {
    uint64 t1 = timer_get_ticks();
    if(t1 != last) {
        printf("[cpu%d] ticks=%d\n", id, t1);
        last = t1;
    }
}
```

------

## 5. 实验结果与分析

### 5.1 Timer 输出

![image-20251021202854766](C:\Users\34302\AppData\Roaming\Typora\typora-user-images\image-20251021202854766.png)

- hart0 更新全局 tick，hart1 可读取观察。
- Timer 中断周期正确触发，S-mode trap 工作正常。

### 5.2 UART 中断响应测试

- 在 QEMU 终端输入字符：

![image-20251021202921846](C:\Users\34302\AppData\Roaming\Typora\typora-user-images\image-20251021202921846.png)

- 每个字符通过 S-mode trap 的 external_interrupt_handler 读取并回显，说明 UART interrupt 在多核系统中工作正常。

------

## 6. 实验总结

- 成功实现多核 S-mode trap 的 timer 和 UART interrupt 管理。
- 理解 hart 启动顺序、trap 配置及中断触发流程。
- Timer interrupt 由 hart0 更新系统 tick，其他 hart 可读取，验证多核定时器功能。
- UART 中断能够实时响应键盘输入并回显，说明外设中断在 S-mode trap 下可靠。
- 实验对 RISC-V 多核中断系统和时钟管理有了直观理解和实践经验。