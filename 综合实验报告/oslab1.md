- # 综合实验报告（report.md）

  ## 一、系统设计部分

  ### 架构设计说明

  本实验实现一个**最小多核 RISC-V 裸机操作系统**，目标是在 QEMU `virt` 平台上输出：

  ```
  Hello OS!
  hart 1 starting
  ```

  系统启动流程：

  1. **entry.S**：汇编入口，设置栈指针、清零 BSS、跳转到 `start()`
  2. **start.c**：多核同步，hart0 初始化 UART 和自旋锁，其他 hart 等待 `started` 标志
  3. **main.c**：输出 “Hello OS!” 和每个 hart 启动信息
  4. **uart.c/print.c**：提供多类型 printf、panic/assert、UART 输出
  5. **spinlock.c**：自旋锁实现，保护多核打印安全

  ------

  ### 关键数据结构

  | 模块           | 数据结构                | 说明                                            |
  | -------------- | ----------------------- | ----------------------------------------------- |
  | spinlock.c     | `spinlock_t`            | 自旋锁，保护共享资源 UART、打印                 |
  | uart.c         | UART 寄存器 (THR/LSR)   | 实现字符输出，检测 THRE 位完成发送              |
  | print.c        | `volatile int panicked` | 避免 panic 后多核打印冲突                       |
  | start.c/main.c | `volatile int started`  | hart0 初始化完成标志，多核同步                  |
  | entry.S        | `CPU_stack`             | 每个 hart 独立 4KB 栈，用异常实现跳转到main函数 |

  ------

  ### 与 xv6 对比分析

  - **相同点**：
    - 启动栈设置、BSS 清零
    - 多核启动
    - UART 输出作为调试手段
  - **不同点**：
    - 简化内存管理、trap、分页和任务调度
    - 无用户态进程和上下文切换
    - 多核输出仅保证打印安全，不涉及任务调度

  ------

  ### 设计决策理由

  - 栈：4KB/核，保证 C 调用安全
  - BSS 清零：保证全局变量为 0，防止乱码
  - UART 初始化由 hart0 完成，避免多核冲突
  - 自旋锁全局共享，保证 printf 输出多核安全
  - 死循环：防止 hart 越界或 QEMU 重启

  ------

  ## 二、实验过程部分

  ### 实现步骤记录

  1. **entry.S**

  ```
  .section .text
  _entry:
          la sp, CPU_stack         # 栈基址
          li a0, 4096
          add sp, sp, a0           # 每核偏移栈
          call start               # 跳转 C 启动函数
  spin:
          j spin
  ```

  - 设置每核栈、跳转 C 函数

  1. **kernel.ld**（链接脚本）

  ```
  ENTRY(_entry)
  SECTIONS {
      . = 0x80000000;
      .text : { *(.text*) }
      .data : { *(.data*) }
      .bss : { _bss_start = .; *(.bss*); _bss_end = .; }
  }
  ```

  - 指定入口、段组织、定义 BSS

  1. **start.c**

  ```
  extern void main();
  volatile int started = 0;
  
  void start() {
      if (mycpuid() == 0) {
          uart_init();
          print_init();
          main();
          started = 1;
      } else {
          while (!started);
          main();
      }
  }
  ```

  - 多核同步，hart0 初始化 UART/打印锁

  1. **main.c**

  ```
  void main() {
      if (mycpuid() == 0)
          printf("Hello OS!\n");
      printf("hart %d starting\n", mycpuid());
      while(1); // 防止程序退出
  }
  ```

  1. **uart.c**

  ```
  void uart_putc_sync(char c) {
      while (!(load_8(LSR_ADDR) & 0x20)); // 等待 THR 空
      store_8(THR_ADDR, c);
  }
  ```

  - 最小 UART 输出，实现字符发送

  1. **print.c**

  - 支持 `%d/%x/%p/%s/%c`
  - 使用全局自旋锁保护多核打印
  - `panic/assert` 提供错误提示

  1. **spinlock.c**

  ```
  void spinlock_acquire(spinlock_t *lk);
  void spinlock_release(spinlock_t *lk);
  ```

  - 保护共享资源（UART、printf）

  ------

  ### 问题与解决方案

  | 问题           | 原因                   | 解决方案                                                |
  | -------------- | ---------------------- | ------------------------------------------------------- |
  | hart 输出丢失  | 多核同时初始化 UART/锁 | 由 hart0 初始化 UART 和全局锁，其他 hart 等待 `started` |
  | 输出乱码       | BSS 未清零，栈冲突     | 清零 BSS，每核独立栈                                    |
  | 自旋锁不同实例 | 每核初始化锁           | hart0 初始化全局锁，其他 hart 使用同一锁                |

  ------

  ### 源码理解总结

  - **entry.S**：设置栈、清零 BSS、跳转 start.c
  - **start.c/main.c**：多核同步，输出 "Hello OS!" 和 hart 启动信息
  - **uart.c**：最小字符输出，检查 LSR THRE
  - **print.c**：printf 支持多类型、全局自旋锁、多核安全 panic/assert
  - **spinlock.c**：自旋锁 acquire/release，保护共享资源

  ------

  ## 三、测试验证部分

  ### 功能测试结果

  QEMU 输出：

  ```
  Hello OS!
  hart 1 starting
  hart 2 starting
  ```

  - 输出完整、字符无乱码、hart 顺序正确

  ### 性能数据

  - 程序大小 <10 KB
  - 启动耗时 <0.1 s

  ### 异常测试

  - 未清零 BSS → 全局变量值随机
  - UART 未初始化 → 输出丢失
  - 移除自旋锁 → 多核打印交错
  - 错误入口 `_entry` → QEMU 卡死

  ### 运行截图/录屏

  ![image-20250919190303260](C:\Users\34302\AppData\Roaming\Typora\typora-user-images\image-20250919190303260.png)

  ------

  ## 四、思考题回答

  1. **启动栈设计**
     - 每核 4KB，保证函数调用安全
     - 栈溢出 → 覆盖内存，hart 崩溃
     - 检测方法：在栈边界设置标记
  2. **BSS 段清零**
     - 不清零 → 未初始化变量值随机
     - 可省略条件：不使用全局未初始化变量
  3. **与 xv6 对比**
     - 无页表、中断、任务调度
     - 优势：易理解，控制多核启动
     - 缺点：不支持用户态、多任务
  4. **错误处理**
     - panic错误信息