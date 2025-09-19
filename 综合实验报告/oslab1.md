# 实验1 RISC-V 引导与裸机启动

## 一、实验目标

本实验通过参考 xv6 的启动机制，实现一个最小操作系统的多核启动，并在 QEMU `virt` 平台上输出：

```
Hello OS!
hart 1 starting
```

实验目标包括：

- 理解 RISC-V 启动机制和裸机多核引导
- 学习栈初始化、BSS 清零和链接脚本设计
- 实现最小 UART 驱动和 `printf`/`panic` 输出机制
- 理解多核共享资源访问和自旋锁使用

------

## 二、系统设计部分

### 架构设计说明

系统运行在 **RISC-V M-mode**，每个 hart 启动流程如下：

1. **entry.S**：设置栈指针、跳转到 C 语言主函数
2. **start.c/main()**：
   - hart0 初始化 UART 和打印锁
   - 设置 `started` 标志，通知其他 hart
   - 各 hart 调用 `printf` 输出启动信息
3. **死循环**：防止程序意外退出

### 关键数据结构

- **UART 寄存器**
  - `THR (0x10000000)`：发送字符
  - `LSR (0x10000005)`：检查发送完成
- **栈**
  - 每个 hart 使用独立 4KB 栈
- **链接符号**
  - `_entry`：程序入口
  - `_bss_start`、`_bss_end`：BSS 段清零范围
- **自旋锁**
  - `print_lk`：保护多核同时输出 UART

### 与 xv6 对比

- **相同点**
  - 启动栈设置、BSS 清零
  - UART 输出验证启动成功
- **不同点**
  - 本实验简化内存管理、trap 和中断处理
  - 多核支持仅限打印同步，不涉及任务调度

### 设计决策理由

- 栈：保证 C 调用正确
- BSS 清零：保证全局未初始化变量为 0
- UART 输出：提供最直观调试方式
- 死循环：防止 hart 执行越界或系统重启
- 自旋锁：保证多核打印不会交错

------

## 三、实验过程部分

### 实现步骤

1. **启动汇编 entry.S**

```
.section .text
_entry:
        la sp, CPU_stack         # 栈基址
        li a0, 4096
        add sp, sp, a0
        call start               # 跳转到 C 语言启动函数
spin:
        j spin
```

1. **链接脚本 kernel.ld**

```
ENTRY(_entry)
SECTIONS {
    . = 0x80000000;
    .text : { *(.text*) }
    .data : { *(.data*) }
    .bss : { _bss_start = .; *(.bss*); _bss_end = .; }
}
```

1. **UART 驱动和打印**

- `uart_putc_sync`：发送字符前检查 LSR 的 THRE 位
- `printf`：支持 `%d`, `%x`, `%p`, `%s`, `%c`，支持 `long` / `long long`
- 使用全局自旋锁 `print_lk`，保证多核打印安全

1. **主函数 main.c**

```
volatile int started = 0;

void main() {
    if (mycpuid() == 0) {
        uart_init();
        print_init();
        printf("Hello OS!\n");
        __sync_synchronize();
        started = 1;
    } else {
        while (!started);
    }

    printf("hart %d starting\n", mycpuid());
    while (1);
}
```

### 问题与解决方案

| 问题               | 解决方案                                        |
| ------------------ | ----------------------------------------------- |
| 输出乱码或丢失     | hart0 初始化 UART，其他 hart 等待 `started`     |
| 自旋锁不同实例     | 全局锁由 hart0 初始化，其他 hart 使用同一锁     |
| BSS 未清零         | 在 `start()` 中清零 `_bss_start`~`_bss_end`     |
| 多核输出顺序不可控 | 可按 hartid 顺序打印或通过 `started` 分阶段输出 |

### 源码理解总结

- **entry.S**：设置栈、跳转到 C，保证 M-mode 能执行 C 函数
- **main.c/start.c/uart.c**：多核同步、UART 初始化
- **print.c**：全局自旋锁保护、多类型 printf、多核安全 panic/assert

------

## 四、测试验证部分

### 功能测试结果

QEMU 输出：

```
Hello OS!
hart 1 starting
```

输出完整，字符无乱码。

### 性能数据

- 程序大小 < 10KB
- 启动耗时 < 0.1s

### 异常测试

- 去掉 BSS 清零 → 全局变量值异常
- 错误入口点 → QEMU 卡死
- 未检查 LSR → 输出丢字符

### 运行截图

![image-20250919175539606](C:\Users\34302\AppData\Roaming\Typora\typora-user-images\image-20250919175539606.png)

------

## 五、思考题回答

### 1. 启动栈的设计

- 栈大小：4KB 足够嵌套调用
- 栈溢出：覆盖内存，可能导致 hart 崩溃
- 检测：栈边界设置保护值

### 2. BSS 段清零

- 不清零 → 未初始化全局变量值随机
- 可省略条件：全局变量不被使用，但一般不推荐

### 3. 与 xv6 对比

- 简化部分：页表、trap、中断、多进程调度
- 可能问题：后续多任务和用户态运行受限

### 4. 错误处理

- UART 初始化失败 → 死循环或复位
- 最小错误显示机制 → 串口打印 `'E'` 或点亮 I/O 指示灯

------

## 六、结论

- 成功实现多核裸机启动
- 全局打印机制和 panic/assert 多核安全
- 学习了栈初始化、BSS 清零、UART 驱动、链接脚本设计
- 为后续多核操作系统开发打下基础