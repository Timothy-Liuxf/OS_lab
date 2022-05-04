# lab1

---

2019011008

无92 刘雪枫

---

## 目录

[TOC]

## 新增代码

切换到分支 `ch3` 编写代码



### 数据结构

首先需要增加 `sys_task_info` 调用参数的类型定义。  

在 `syscall_ids.h` 中加入系统调用数的最大值：  

```c
// syscall_ids.h

#define MAX_SYSCALL_NUM 500
```

新建文件 `taskdef.h`，增加任务状态 `TaskStatus` 与任务信息 `TaskInfo` 结构（两个类型的定义与 `user/include/stddef.h` 中的定义保持一致）：  

```c
// taskdef.h

#ifndef TASKDEF_H
#define TASKDEF_H

#include "syscall_ids.h"

typedef enum {
    UnInit,
    Ready,
    Running,
    Exited,
} TaskStatus;

typedef struct {
    TaskStatus status;
    unsigned int syscall_times[MAX_SYSCALL_NUM];
    int time;
} TaskInfo;

#endif // !TASKDEF_H
```



### 存储进程的相关信息

系统调用需要获取进程的状态、系统调用次数，以及运行时间。由于只有正在运行的进程才能进行系统调用，因此获取的进程的状态始终为 `Running`。  

系统调用次数，需要每个进程开辟空间来存储各个系统调用的次数；而运行时间则通过存储每个进程启动时的运行时间（以 CPU 周期计）来实现，在进行 `sys_task_info` 调用时用当前时间减去启动的时间即可。  

因此，修改 `struct proc` 结构，讲进程的运行时间和系统调用次数也储存在进程块内：  

```c
// proc.h

struct proc {
    enum procstate state;
    int pid;
    uint64 ustack;
    uint64 kstack;
    struct trapframe *trapframe;
    struct context context;
    uint64 cycles_when_start;                        // 进程运行时间（以 CPU 周期计）
    unsigned int syscall_times[MAX_SYSCALL_NUM];     // 系统调用次数
};
```



### 编写系统调用

然后编写真正的内核函数 `sys_task_info` 实现此系统调用，获取进程信息——计算进程运行时间以及复制系统调用次数：  

```c
// syscall.c

int sys_task_info(TaskInfo *ti)
{
    struct proc *proc_ptr = curr_proc();
    ti->status = Running;
    memmove(ti->syscall_times, proc_ptr->syscall_times,
        sizeof(ti->syscall_times));
    uint64 diff = get_cycle() - proc_ptr->cycles_when_start;
    ti->time = (int)(diff / (CPU_FREQ / 1000));
    return 0;
}
```

还需要修改系统调用函数 `syscall` 把系统调用号为 `SYS_task_info` 的调用映射到 `sys_task_info` 上：  

```c
// syscall.c

void syscall()
{
    /* ... */
    case SYS_task_info:
        ret = sys_task_info((TaskInfo *)args[0]);
        break;
    /* ... */
}
```



### 实现系统调用计数

实现对系统调用的次数进行计数，就需要每次系统调用时将计数加一。而每次系统调用都需要经过 `syscall` 函数，因此在 `syscall` 函数内进行计数：  

```c
// syscall.c

void syscall()
{
    /* ... */
    struct proc *proc_ptr = curr_proc();
    struct trapframe *trapframe = proc_ptr->trapframe;
    int id = trapframe->a7, ret;
    /* ... */
    if (id < MAX_SYSCALL_NUM) {
        ++proc_ptr->syscall_times[id];  // 本次系统调用计数自增
    }
    /* ... */
}
```



### 初始化进程块

在分配进程时，需要对新添加的成员初始化——需要记录进程起始时间（使用 `get_cycle`），并将系统调用次数清零（使用 `memset`）。代码添加在 `alloc_proc` 中：  

```c
// proc.c

void alloc_proc(void)
{
    /* ... */
    p->cycles_when_start = get_cycle();
    memset(p->syscall_times, 0, sizeof(p->syscall_times));
}
```



### 优化 `memset`

编写代码时发现，初始化进程块时用到的 `memset` 是逐个字节拷贝，效率较低，因此，参考 glibc 中实现（[glibc/memset.c at master · lattera/glibc (github.com)](https://github.com/lattera/glibc/blob/master/string/memset.c)）的思想进行优化，当拷贝的长度过长时改逐个字节拷贝为按每 8 个字（每个字 64 位）进行拷贝：  

```c
// string.c

static void *fast_memset(void *dst, int val, uint cnt)
{
    char *sp = (char *)dst; // start pos
    char *ep = sp + cnt;

    // if cnt is too large, fast copy; otherwise, trivial copy

    if (cnt > 32) {
        // align to 8 bytes
        uint64 r = (8 - (uint64)sp % 8) % 8;
        while (r != 0) {
            *sp++ = val;
            --r;
        }

        // fast copy
        uint64 val64 = val & 0xFF;
        val64 |= val64 << 8;
        val64 |= val64 << 16;
        val64 |= val64 << 32;

        r = (uint64)(ep - sp) / 8;

        int tmp = r % 8;
        for (int i = 0; i < tmp; ++i) {
            *(uint64 *)sp = val64;
            sp += 8;
        }
        r -= tmp;

        while (r > 0) {
            ((uint64 *)sp)[0] = val64;
            ((uint64 *)sp)[1] = val64;
            ((uint64 *)sp)[2] = val64;
            ((uint64 *)sp)[3] = val64;
            ((uint64 *)sp)[4] = val64;
            ((uint64 *)sp)[5] = val64;
            ((uint64 *)sp)[6] = val64;
            ((uint64 *)sp)[7] = val64;
            sp += 64, r -= 8;
        }
    }

    // copy others
    while (sp != ep) {
        *sp++ = val;
    }

    return dst;
}

void *memset(void *dst, int c, uint n)
{
    return fast_memset(dst, c, n);
}
```



## 运行结果

运行命令：  

```shell
$ make test BASE=0 CHAPTER=3
```

得到如下结果：  

```
qemu-system-riscv64 -nographic -machine virt -bios ./bootloader/rustsbi-qemu.bin -kernel build/kernel 
[rustsbi] RustSBI version 0.1.1
.______       __    __      _______.___________.  _______..______   __
|   _  \     |  |  |  |    /       |           | /       ||   _  \ |  |
|  |_)  |    |  |  |  |   |   (----`---|  |----`|   (----`|  |_)  ||  |
|      /     |  |  |  |    \   \       |  |      \   \    |   _  < |  |
|  |\  \----.|  `--'  |.----)   |      |  |  .----)   |   |  |_)  ||  |
| _| `._____| \______/ |_______/       |__|  |_______/    |______/ |__|

[rustsbi] Platform: QEMU (Version 0.1.0)
[rustsbi] misa: RV64ACDFIMSU
[rustsbi] mideleg: 0x222
[rustsbi] medeleg: 0xb1ab
[rustsbi-dtb] Hart count: cluster0 with 1 cores
[rustsbi] Kernel entry: 0x80200000
Hello world from user mode program!
Test hello_world OK!
3^10000=5079
3^20000=8202
3^30000=8824
3^40000=5750
get_time OK! 72
current time_msec = 74
AAAAAAAAAA [1/5]
CCCCCCCCCC [1/5]
BBBBBBBBBB [1/5]
3^50000=3824
3^60000=8516
AAAAAAAAAA [2/5]
CCCCCCCCCC [2/5]
BBBBBBBBBB [2/5]
3^70000=2510
3^80000=9379
3^90000=2621
3^100000=2749
Test power OK!
AAAAAAAAAA [3/5]
CCCCCCCCCC [3/5]
BBBBBBBBBB [3/5]
CCCCCCCCCC [4/5]
BBBBBBBBBB [4/5]
AAAAAAAAAA [4/5]
CCCCCCCCCC [5/5]
BBBBBBBBBB [5/5]
AAAAAAAAAA [5/5]
Test write C OK!
Test write B OK!
Test write A OK!
time_msec = 176 after sleeping 100 ticks, delta = 102ms!
Test sleep1 passed!
hello task!
Test task info OK!
Test sleep OK!
[PANIC 5] os/loader.c:14: all apps over
```

可以看到程序正常运行，说明编写的代码正确  



## 思考题

### 1

本实验选用的 `RustSBI` 版本为 `0.1.1`

在 U 态使用 S 态指令的错误代码位于 `__ch2_bad_address.c`、`__ch2_bad_instruction.c` 和 `__ch2_bad_register.c` 三个文件里。阅读 `user` 目录内的 `Makefile` 可知，当指定 `CHAPTER` 为 `2_BAD` 时，会编译这三个应用程序。  

先切换到分支 `ch2`。为了单独验证三种不同错误，当测试一个错误程序时，把另外两个程序的 `main` 中的代码注释掉。  

1. 测试 `__ch2_bad_address.c`：  

   注释掉另外两个程序的 `main` 中的代码，仅观察本程序的代码：  

   ```c
   int main()
   {
       int *p = (int *)0;
       *p = 0;
       return 0;
   }
   ```

   可以看到，本程序中用户态访问了无法访问的内存为 0 处的地址。执行命令：  

   ```shell
   $ make clean && make test CHAPTER=2_bad
   ```

   得到报错信息：  

   ```
   [ERROR 0]unknown trap: 0x0000000000000007, stval = 0x0000000000000000 sepc = 0x0000000080400004
   ```

2. 测试 `__ch2_bad_instruction.c`：  

   该程序代码为：  

   ```c
   int main()
   {
       asm volatile("sret");
       return 0;
   }
   ```

   可以看到，该程序在 U 态使用了 S 态特权指令 `sret`，即试图从 S 态返回 U 态的指令。运行程序，可以得到出错信息：  

   ```
   [rustsbi-panic] hart 0 panicked at 'invalid instruction, mepc: 0000000080400004, instruction: 0000000010200073', platform/qemu/src/main.rs:458:17
   [rustsbi-panic] system shutdown scheduled due to RustSBI panic
   ```

3. 测试 `__ch2_bad_register.c`：  

   该程序代码为：  

   ```c
   int main()
   {
       uint64 x;
       asm volatile("csrr %0, sstatus" : "=r"(x));
       return 0;
   }
   ```

   可以看到，该程序在 U 态使用特权指令 `csrr` 试图访问寄存器 `sstatus` 导致异常的产生。运行程序，可以得到出错信息：  

   ```
   [rustsbi-panic] hart 0 panicked at 'invalid instruction, mepc: 0000000080400004, instruction: 00000000100027f3', platform/qemu/src/main.rs:458:17
   [rustsbi-panic] system shutdown scheduled due to RustSBI panic
   ```

   

### 2

#### 2.1

刚进入`userret` 时，`a0` 的值应为保存中断产生时 U 态 `trapframe` 的地址，而 `a1` 应当为以后实现虚拟内存时传递用户态进程页表的地址而保留——由于在 `ch3` 以前没有实现页表和虚拟内存，因此 `a1` 并未使用  

#### 2.2

`sfence.vma zero, zero` 表示刷新所有的 TLB 快表。因为当使用虚拟内存时，需要使用页表来存储虚拟内存与物理内存的映射关系，而在不同的进程切换时，每个进程使用的页表是不同的，故 TLB 内的数据失效，因此需要刷新所有的 TLB 表项。但是在本章节并没有实现页表和虚存机制，因此删除此指令不会有影响。  

#### 2.3

除去 `a0` 是因为在 `uservec` 中是因为 `a0` 需要用于储存 `trapframe` 的地址用于访问 `trapframe`，之后需要使用 `csrrw a0, sscratch, a0` 来恢复 `a0` 并存储 `trapframe` 的地址。`112(a0)` 代表的是寄存器 `a0`。当前 `sscratch` 中保存着当时 `a0` 的值，在恢复其他寄存器之后，使用 `csrrw a0, sscratch, a0` 可以交换 `sscratch` 和 `a0` 达到恢复 `a0` 寄存器和储存 `trapframe` 地址的目的。  

#### 2.4

`userret` 中发生状态切换发生在 `sret` 指令。该指令会设置 S 态为 U 态，并将程序计数器设置为 `sepc` 内的值，即跳转到发生 `trap` 处的位置（或下一条指令的位置），恢复用户态的执行。  

#### 2.5

`uservec` 中的该指令执行之后， `a0` 变为 `trapframe` 的地址，而 `sscratch` 变为原来的 `a0` 寄存器的值。因为以前在 `userret` 中进入 U 态之前，`sscratch` 被设置为 `trapframe` 的地址，因此在 U 态陷入到 S 态时，`sscratch` 内仍然是该地址，而 `csrrw a0, sscratch, a0` 指令可以交换 `a0` 和 `sscratch` 的值。  

#### 2.6

观察 `struct trapframe` 的定义：  

```c
struct trapframe {
    /*   0 */ uint64 kernel_satp;
    /*   8 */ uint64 kernel_sp;
    /*  16 */ uint64 kernel_trap;
    /*  24 */ uint64 epc;
    /*  32 */ uint64 kernel_hartid;
    /*  40 */ uint64 ra;
    /*  48 */ uint64 sp;

        /* ... */
    
    /* 272 */ uint64 t5;
    /* 280 */ uint64 t6;
};
```

可以看到，第一项 `sd ra, 40(a0)` 是从第六项 `ra` 开始保存的。这段代码目的是保存发生 `trap` 时的各个寄存器的值。但是这段代码并没有保存 `a0`。这是因为此时 `a0` 的值是 `trapframe` 的地址，而不是发生陷入时的 `a0` 的值。发生陷入时 `a0` 的值被暂存在了 `sscratch` 寄存器内。  

#### 2.7

进入 S 态指令是 `ecall` 指令发生的。在执行 `ecall` 时，会从 U 态进入 S 态，并且程序跳转到寄存器 `stvec` 内的地址，该寄存器在执行 `trap_init` 函数时被设置为 `uservec`：  

```c
void trap_init(void)
{
    w_stvec((uint64)uservec & ~0x3);
}
```

#### 2.8

在该汇编程序中存在指令：  

```asm
ld t0, 16(a0)
jr t0
```

对照 `struct trapframe` 的定义可以知道，`16(a0)` 是取 `kernel_trap` 的值。在 `trap.c` 的 `usertrapret` 函数中存在代码：  

```c
trapframe->kernel_satp = r_satp();
trapframe->kernel_sp = kstack + PGSIZE;
trapframe->kernel_trap = (uint64)usertrap;
trapframe->kernel_hartid = r_tp();
```

可以看到 `kernel_trap` 被设置为了 `usertrap` 的地址。而 `usertrapret` 会在 `run_next_app` 和 `usertrap` 函数中调用，即运行下一个程序，或发生陷入时，`kernel_trap` 会被设置。因此，原汇编代码中的 `t0` 的值就是 `usertrap` 的地址。 

