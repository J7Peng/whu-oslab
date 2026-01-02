# 实验6——进程管理实验报告

------

## 一、实验目标

本次实验旨在完善操作系统内核中的进程管理模块，实现多进程的并发执行、状态管理及基本的进程间同步机制。具体目标包括：

1. 实现进程创建、终止和等待相关的核心系统调用，包括 `fork`、`exit` 和 `wait`。
2. 实现多进程的调度机制，确保 CPU 资源的公平分配。
3. 实现进程间的阻塞与唤醒机制（`sleep` 和 `wakeup`），以支持更高效的资源等待和条件同步。

## 二、进程管理核心系统调用实现

本实验实现了除 `mmap` 外的所有关键进程管理系统调用，以下为核心函数的机制描述。

### 1. 进程结构体 `proc_t`

进程结构体 `proc_t` 是进程控制块的核心，用于存储和管理一个进程的所有信息。关键字段包括：

- `pid`：进程标识符。
- `state`：进程当前状态（`enum proc_state`）。
- `parent`：指向父进程的指针。
- `pgtbl`：进程的用户态页表基址。
- `tf`：陷阱帧（Trap Frame），用于保存用户态/内核态切换时的 CPU 寄存器上下文。
- `ctx`：内核态进程上下文，用于内核线程间的切换（`swtch`）。
- `lk`：用于保护进程状态和字段的自旋锁。

### 2. 进程创建 (`sys_fork`)

`sys_fork` 实现了进程的复制创建机制，关键步骤为：

1. 调用 `proc_alloc` 从进程数组中获取一个空闲的 `proc_t` 结构并初始化其内核栈和页表。
2. **内存空间复制**：将父进程的用户态内存空间（包括代码段、数据段、堆和栈）完整复制到子进程的新页表中。
3. **上下文复制**：复制父进程的陷阱帧 `tf`，确保子进程从与父进程中断点一致的位置开始执行。
4. **父子返回值区分**：
   - 父进程：`sys_fork` 返回新创建的子进程 `pid`。
   - 子进程：通过修改其陷阱帧中的返回值寄存器（`a0`），使其在返回用户态时，`fork` 函数返回值为 0。
5. 设置子进程状态为 `RUNNABLE`，使其具备被调度的资格。

### 3. 进程终止与等待 (`sys_exit` 与 `sys_wait`)

- **进程终止 (`sys_exit`)**：
  - 设置进程状态为 `ZOMBIE`，保留其 PID 和退出状态。
  - 调用 `proc_reparent` 将其所有子进程托付给初始化进程 `proczero`（处理孤儿进程）。
  - 调用 `proc_wakeup_one` 唤醒等待该进程终止的父进程。
  - 调用 `proc_sched` 让出 CPU。
- **进程等待 (`sys_wait`)**：
  - 父进程扫描子进程列表，查找处于 `ZOMBIE` 状态的子进程。
  - 若找到僵尸子进程，获取其退出状态，并调用 `proc_free` 回收该子进程的所有资源（页表、堆栈、`proc_t` 结构体），最后返回子进程的 `pid`。
  - 若未找到已终止的子进程，父进程调用 `proc_sleep` 进入睡眠状态，等待子进程退出时被唤醒。

## 三、进程调度与同步机制

### 1. 进程状态转换

本实验实现的进程状态模型支持状态间的转换，确保进程管理系统的高效性。

进程状态转换图如下所示：

| **状态**     | **描述**                                              | **关键转换**                                                 |
| ------------ | ----------------------------------------------------- | ------------------------------------------------------------ |
| **UNUSED**   | 进程结构体未被使用。                                  | $\to$ **RUNNABLE** (`proc_alloc`)                            |
| **RUNNABLE** | 进程已就绪，等待被调度执行。                          | $\to$ **RUNNING** (`proc_scheduler` 选中)                    |
| **RUNNING**  | 进程正在 CPU 上执行。                                 | $\to$ **SLEEPING** (`proc_sleep`) / $\to$ **RUNNABLE** (`proc_yield`) / $\to$ **ZOMBIE** (`proc_exit`) |
| **SLEEPING** | 进程被阻塞，等待特定事件（如 I/O、`wait` 事件）发生。 | $\to$ **RUNNABLE** (`proc_wakeup`)                           |
| **ZOMBIE**   | 进程已终止，但其 `proc_t` 结构体尚未被父进程回收。    | $\to$ **UNUSED** (`proc_free` 被父进程调用)                  |

### 2. 进程调度

系统采用**时间片轮转（Round Robin, RR）**调度算法。

- 通过**时钟中断**触发调度。在时钟中断处理程序中，调用 `proc_yield()`。
- `proc_yield()` 将当前 `RUNNING` 进程的状态置为 `RUNNABLE`，并调用 `proc_sched()`。
- `proc_sched()` 负责进行上下文切换：首先切换到 CPU 的调度器上下文，然后调度器 (`proc_scheduler`) 循环查找下一个 `RUNNABLE` 进程，并通过 `swtch` 函数实现内核态上下文的切换。

### 3. 阻塞与唤醒 (`sleep` 和 `wakeup`)

`sleep` 和 `wakeup` 机制用于实现条件同步，避免 CPU 自旋等待。

- **`proc_sleep(chan)`**：将当前进程状态设置为 `SLEEPING`，并将等待事件地址（`chan`）记录在 `sleep_space` 字段，随后调用 `proc_sched` 放弃 CPU。
- **`proc_wakeup(chan)`**：遍历所有进程，将所有 `sleep_space` 字段与 `chan` 地址匹配的 `SLEEPING` 进程状态设置为 `RUNNABLE`，使其具备被再次调度的资格。

**同步互斥**：为避免进程在进入睡眠和被唤醒之间发生竞态条件而丢失唤醒信号，`sleep` 机制必须依赖于一把保护锁（Guard Lock）。进程在调用 `proc_sleep` 时必须持有锁，在设置完 `SLEEPING` 状态后释放锁，从而保证原子操作。

## 四、测试代码分析与验证

以下是对提供的测试 C 语言代码的分析，该代码用于验证 `brk`、`fork`、`exit` 和 `wait` 系统调用的正确性。

### 1. 堆管理 (`sys_brk`) 验证

C

```
// 测试HEAP区域
long long top = syscall(SYS_brk, 0);
str2 = (char*)top;
syscall(SYS_brk, top + PGSIZE);
// ...
str2[0] = 'H'; // ...
```

- **验证目标**：测试进程能否通过 `SYS_brk` 扩展其堆（Heap）区域并使用新分配的内存。
- **机制**：`SYS_brk(0)` 获取当前堆的顶部地址（即 `top`）。再次调用 `SYS_brk(top + PGSIZE)` 成功将堆大小增加一页（4096字节），使得 `str2` 指针可安全写入。

### 2. 进程创建、终止与等待 (`sys_fork`, `sys_exit`, `sys_wait`) 验证

C

```
int pid = syscall(SYS_fork);

if(pid == 0) { // 子进程
    // ...
    syscall(SYS_print, str2); // 验证内存继承
    syscall(SYS_exit, 1);
} else { // 父进程
    int exit_state;
    syscall(SYS_wait, &exit_state);
    if(exit_state == 1)
        syscall(SYS_print, "parent: hello\n");
    // ...
}
```

- **`sys_fork` 验证**：`pid = syscall(SYS_fork)` 成功将控制流分叉。父进程获得子进程 PID（`pid > 0`），子进程获得返回值 0。
- **内存空间继承验证**：子进程中打印 `str2`（内容为 "HEAP\n"），验证了在 `fork` 之前对堆区域的写入操作，能被子进程通过**内存复制**机制正确继承。
- **`sys_exit` 和 `sys_wait` 验证**：
  - 子进程执行完毕后调用 `syscall(SYS_exit, 1)` 终止，并将退出状态设置为 1。
  - 父进程通过 `syscall(SYS_wait, &exit_state)` 阻塞，直到子进程退出。成功回收子进程资源后，`exit_state` 应为 1。
  - 若父进程打印 `"parent: hello\n"`，则证明 `wait` 机制正确阻塞和唤醒了父进程，并且成功获取了子进程的退出状态（1）。

## 五、实验总结与反思

本次实验成功实现了操作系统进程管理的核心功能，包括进程创建（`fork`）、终止（`exit`）、等待（`wait`）、堆扩展（`brk`）、进程调度以及阻塞与唤醒机制（`sleep`/`wakeup`）。