# lab1

---

2019011008

无92 刘雪枫

---

## 目录

[TOC]

## 新增代码

切换到分支 `ch4` 编写代码

### 实现 `gettimeofday` 系统调用

由于 `sys_gettimeofday` 传进来的是虚拟地址，不能直接向里面写入值，所以原来的实现失效。因此，先开辟一个 `TimeVal` 的缓冲区用于保存结果，然后使用 `copyout` 函数将其复制到传入的虚存地址中：  

```c
uint64 sys_gettimeofday(
	TimeVal *val,
	int _tz)
{
	struct proc *p = curr_proc();

	TimeVal val_impl;
	uint64 cycle = get_cycle();
	val_impl.sec = cycle / CPU_FREQ;
	val_impl.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	return copyout(p->pagetable, (uint64)val, (char *)&val_impl,
		       sizeof(*val));
}
```



### 实现 `mmap` 系统调用

首先在 `syscall` 中加入对该系统调用的映射：  

```c
void syscall()
{
	/* ... */
    case SYS_mmap:
		ret = sys_mmap((void *)args[0], (uint64)args[1], (int)args[2],
			       (int)args[3], (int)args[4]);
		break;
    /* ... */
}
```

然后编写 `sys_mmap` 函数。  

函数中，首先要检查输入是否合法：`len` 的长度是否在 `0` 到 `1GiB` 之间、传入的地址 `addr` 是否对齐到页（`((uint64)addr & (PAGE_SIZE - 1)) == 0`）、传入的权限控制位需要至少有读、写、执行之中的一个（`(port & 0x7) != 0`）、除了该三个位之外其他位必须都是 `0`（`(port & ~0x7) == 0`）。  

在进行物理页面的分配时使用 `kalloc` 逐页分配。在映射虚拟页面时，将 `perm` 置为传入的 `port` 指定的权限，以及允许用户态访问：`(port << 1) | PTE_U`。最后编写的代码如下：  

```c
uint64 sys_mmap(void *addr, uint64 len, int port, int flag, int fd)
{
	// Unused parameter
	(void)flag;
	(void)fd;

	if (len == 0) {
		return 0;
	}
	if (len > ((uint64)1 << 30)) {	// Greater than 1 GiB
		return -1;
	}

	if (((uint64)addr & (PAGE_SIZE - 1)) != 0) {
		return -1;
	}

	if ((port & ~0x7) != 0) {
		return -1;
	}

	if ((port & 0x7) == 0) {
		return -1;
	}

	len = PGROUNDUP(len);
	uint64 end = (uint64)addr + len;
	pagetable_t pg = curr_proc()->pagetable;

	for (uint64 va = (uint64)addr; va != end; va += PAGE_SIZE) {
		void *pa = kalloc();
		if (pa == 0) {
			return -1;
		}

		if (mappages(pg, va, PAGE_SIZE, (uint64)pa,
			     (port << 1) | PTE_U) != 0) {
			return -1;
		}
	}
	return 0;
}
```



### 实现 `munmap` 系统调用

首先在 `syscall` 中加入对该系统调用的映射：  

```c
void syscall()
{
	/* ... */
	case SYS_munmap:
		ret = sys_munmap((void *)args[0], (uint64)args[1]);
		break;
    /* ... */
}
```

然后编写 `sys_munmap` 函数。先调用 `walkaddr` 判断是否存在用户态可访问的映射（使用 `walkaddr` 而不是 `useraddr` 是因为判断的地址已经设为了每个页面的起始位置），如果不存在则返回 `-1`，否则删除映射，并且释放物理内存：  

```c
uint64 sys_munmap(void *addr, uint64 len)
{
	if (len == 0) {
		return 0;
	}
	if (len > ((uint64)1 << 30)) {	// Greater than 1 GiB
		return -1;
	}

	if (((uint64)addr & (PAGE_SIZE - 1)) != 0) {
		return -1;
	}

	len = PGROUNDUP(len);
	uint64 end = (uint64)addr + len;
	pagetable_t pg = curr_proc()->pagetable;

	for (uint64 va = (uint64)addr; va != end; va += PAGE_SIZE) {
		uint64 pa = walkaddr(pg, va);
		if (pa == 0) {
			return -1;
		}
		uvmunmap(pg, va, 1, 1);
	}
	return 0;
}
```



### 重新实现 `ch3` 中的 `sys_task_info`

由于在本章使用了虚拟内存，因此 `ch3` 中的 `sys_task_info` 不再适用，原因与 `sys_gettimeofday` 类似。在把 `ch3` 的代码 `merge` 到 `ch4` 并解决冲突后，仿照 `sys_gettimeofday` 对代码进行修改：  

```c
int sys_task_info(TaskInfo *ti)
{
	struct proc *proc_ptr = curr_proc();
	TaskInfo ti_impl;
	ti_impl.status = Running;
	memmove(ti_impl.syscall_times, proc_ptr->syscall_times,
		sizeof(ti_impl.syscall_times));
	uint64 diff = get_cycle() - proc_ptr->cycles_when_start;
	ti_impl.time = (int)(diff / (CPU_FREQ / 1000));
	return copyout(proc_ptr->pagetable, (uint64)ti, (char *)&ti_impl,
		       sizeof(*ti));
}
```



## 运行结果

运行命令：  

```shell
$ make test BASE=0 CHAPTER=4 LOG=info | grep Test
```

得到如下测试结果：  

```
Test hello_world OK!
Test power OK!
Test 04_0 OK!
Test 04_4 ummap OK!
Test 04_5 ummap2 OK!
Test 04_3 test OK!
Test write A OK!
Test write C OK!
Test write B OK!
Test sleep1 passed!
Test task info OK!
Test sleep OK!
```

可以看到应当通过的测试全部通过，并且无运行到 fail 的测试  



## 思考题

本实验选用的 `RustSBI` 版本为 `0.1.1`



### 1.  

*请列举 SV39 页表页表项的组成，结合课堂内容，描述其中的标志位有何作用／潜在作用？*

SV39 的页表项共有 64 位。各个位的作用分别如下：  

+ 63\~54 位：最高的十位被保留，暂时不用，设为 0；  
+ 53\~10 位：如果非叶子页表项，则用于储存下一级页表相对于起始位置的偏移量；否则储存的是虚拟页号对应的物理页号；  
+ 9\~8 位：供 supervisor 软件使用；  
+ 7 位：D 位，即脏位，标记该页面是否修改过，若修改过则换出内存页时需写回磁盘；  
+ 6 位：A 位，用于标记该页面是否被访问过，可以用于为页面置换算法提供信息；  
+ 5 位：G 位，用于标记该页的映射内容是全局的，因此在进程切换时不必刷新该页的 TLB 缓存；  
+ 4 位：U 位，用于标记该页是否可以被 U 态（用户态）访问；  
+ 3 位：X 位，用于标记该页的内存是否可以作为指令执行； 
+ 2 位：W 位，用于标记该页的内存是否可以写入；  
+ 1 位：R 位，用于标记该页的内存内容是否可以读取；  
+ 0 位：V 位：用于标记该页表项是否有效，即该页表项对应的虚拟地址范围无效，不存在物理页面的映射。  



### 2. 缺页

+ *请问哪些异常可能是缺页导致的？*

  当用户态进程访问了非用户态的内存、访问了没有进行过虚拟地址到物理地址的映射的内存（没有向操作系统申请过的内存）、尝试执行不位于用户态地址空间的代码、尝试执行的指令地址没有物理页面的映射等，都会因缺页而导致异常。  

+ *发生缺页时，描述相关的重要寄存器的值*

  发生缺页时，`sscratch` 寄存器内是发生缺页的进程的 `trapframe` 的地址；`satp` 是发生缺页的进程的页表的地址；`sepc` 是发生缺页的指令的地址；`mcause` 寄存器中 `Interrupt ` 位是 0，Exception Code 是 12、13 或 15；CPU 位于 S 模式，即值为 `01`

+ *这样做有哪些好处？*

  Lazy 策略有助于节省物理内存空间。因为程序并不总是能够访问到它所占用的所有内存，例如程序执行时，并不是能够执行到代码段内的所有代码。这样，使用 Lazy 策略，可以避免给程序不会访问的内存分配物理页面，从而节省了进程本身以及该进程的页表所占用的物理内存。  

+ *请问处理 10G 连续的内存页面，需要操作的页表实际大致占用多少内存(给出数量级即可)？*

  对于 SV39 多级页表来说，每个页表项是 8 字节，而一个页面是 4096 字节，因此每个物理页面可以容纳 4096 / 8 = 512 个页表项。对于叶子结点所在层次，每个页面代表 512 \* 4096 B = 2MiB，那么 10GiB 就需要 5000 个页面；对于次深层，则每个页面代表 2MiB \* 2^9 = 1GiB，所以需要 10 GiB 内存需要 10 个页面，远小于最深层的 5000 个页面。以 5000 个页面进行估算，大约需要 5000 * 4096 字节，也就是约 20MiB 的内存。  

+ *请简单思考如何才能在现有框架基础上实现 Lazy 策略，缺页时又如何处理？描述合理即可，不需要考虑实现。*

  为每个进程分配一个表，用于记录该进程申请过哪些虚拟内存。当进程申请内存时，并不进行虚拟页到物理页的映射，而是代之以在该表中记录为进程“分配”了哪些虚拟内存。当进程发生缺页异常时，如果进程访问的虚拟地址在表中，那么就修改页表进行虚拟页到物理页的映射，并返回到发生缺页异常的指令处继续执行该进程。  

+ *此时页面失效如何表现在页表项(PTE)上？*

  此时的 PTE 中，V 的值为 0，但 R、W、X 不全为 0  

### 3. 双页表与单页表

+ *单页表情况下，如何更换页表？*  

  单页表情况下，更换页表也是使用 `csrw` 指令将页表地址写入 `satp`，并使用 `sfence.vma zero, zero` 刷新 TLB。但是只有当发生进程的切换时才进行页表的更换。  

+ *单页表情况下，如何控制用户态无法访问内核页面？*

  只需要将内核页面对应的页表项的 U 位置为 0 即可。    

+ *单页表有何优势？*

  单页表情况下，陷入内核不一定会进行页表的切换，只有切换进程的时候才有必要切换页表。这样，切换页表的次数、刷新 TLB 的次数均会减少，可以提高程序执行的效率，减小由于页表切换产生的时间开销以及由于 TLB 缺失产生的时间开销。  

+ *双页表实现下，何时需要更换页表？假设你写一个单页表操作系统，你会选择何时更换页表*  

  双页表实现下，从用户态陷入内核，以及从内核态返回到用户态，都需要更换页表。而单页操作系统，只有当进程发生切换时才有必要进行页表的更换。更换页表可以选择在进程调度之后。当且仅当通过进程调度得到的即将要执行的用户态进程与陷入内核时执行的进程不同时才需要进行页表的更换，否则不需要更换页表。  



