# lab3

---

2019011008

无92 刘雪枫

---

## 目录

[TOC]

## 新增代码

切换到分支 `ch5` 编写代码  

### Merge ch4 与 `mmap` 的更正

运行 `git merge ch4` 将 `ch4` 的代码 merge 到 `ch5`。在解决部分冲突后，修改 `mmap` 的实现。  

由于在进行 `mmap` 时，进程映射的虚拟地址锁对应的虚拟页号可能超过了进程指定的最大页数 `max_page`，但是在取消映射释放内存时，却只从 `0` 释放到 `max_page`，这种情况会导致内存泄漏。因此在进行 `mmap`时，需要根据映射的最大虚拟地址值计算 `max_page`，以防止进程结束后有内存没有被回收。  

假设 `addr` 是进程映射的最大虚拟内存，则应当设置的 `max_page` 为 `PGROUNDUP(addr) / PAGE_SIZE`。  

基于此考虑，`ch5` 所做的更改如下（由于在之前的合法性检查中 `addr` 已经保证了对齐到页，故 `va` 也对齐到也，因此计算 `max_page` 时不需要进行 `PGROUNDUP`）：  

```diff
    /* Check validity if arguments len, addr and port */

    len = PGROUNDUP(len);
 	uint64 end = (uint64)addr + len;
-	pagetable_t pg = curr_proc()->pagetable;
+	struct proc *p = curr_proc();
+	pagetable_t pg = p->pagetable;
+	uint64 va;
+	uint64 ret_val = 0;
 
-	for (uint64 va = (uint64)addr; va != end; va += PAGE_SIZE) {
+	for (va = (uint64)addr; va != end; va += PAGE_SIZE) {
 		void *pa = kalloc();
 		if (pa == 0) {
 			debugf("In sys_mmap: Physical memory is not enough!");
-			return -1;
+			ret_val = -1;
+			goto ret;
 		}
 
 		debugf("In mmap: Will map pa: %p to va: %p", pa, va);
 		if (mappages(pg, va, PAGE_SIZE, (uint64)pa,
 			     (port << 1) | PTE_U) != 0) {
 			debugf("In sys_mmap: Memory map failed!");
-			return -1;
+			ret_val = -1;
+			goto ret;
 		}
 	}
-	return 0;
+
+	ret_val = 0;
+ret:;
+	uint64 new_max_page = va / PAGE_SIZE;
+	if (new_max_page > p->max_page) {
+		p->max_page = new_max_page;
+	}
+	return ret_val;
 }
```

更改之后，运行 ch5_mergetest，可以全部通过。    



### 实现 `spawn` 系统调用 

`spawn` 系统调用传入一个字符串作为程序名称。由于用户态程序传入的地址不能直接使用，因此需要使用 `copyinstr` 复制。在 `syscall.c` 中的 `sys_spawn` 函数中完成该系统调用：  

```c
uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_spawn %s\n", name);
	return spawn(name);
}
```

在 `proc.c` 中编写 `spawn` 函数完成真正的创建进程操作。在该操作中，首先根据程序名获取程序 `id`。如果程序存在则调用 `alloc_proc` 创建新的进程，在调用 `loader` 将程序加载到进程中，再设置新进程的父进程为当前进程，最后调用 `add_task` 将新进程放入任务队列中，并返回新进程的 `pid`。  

代码如下：  

```c
int spawn(char *name)
{
	int id = get_id_by_name(name);
	if (id < 0)
		return -1;
	struct proc *p = curr_proc();
	struct proc *np = allocproc();
	if (np == NULL) {
		panic("In spawn: allocproc failed!\n");
	}

	loader(id, np);
	np->parent = p;
	add_task(np);
	return np->pid;
}
```



### 实现进程优先级与 stride 调度算法

首先修改进程的 PCB，记录进程的优先级 `priority` 与当前 `stride` 值：

```diff
struct proc {
	/* ... */
+	long long priority;
+	long long stride;
};
```

在 `proc.c` 中定义进程优先级初始值、`BIG_STRIDE`、`stride` 的最小值与最大值：  

```c
#define BIG_STRIDE 65536
#define INIT_PRIORITY 16
#define MIN_PRIORITY 2
#define MAX_PRIORITY LLONG_MAX
```

在进程创建时，需要初始化优先级与 `stride`。在 `allocproc` 中进行初始化：  

```c
struct proc *allocproc()
{
    /* ... */
	p->priority = INIT_PRIORITY;
	p->stride = 0;
	return p;
}
```

`scheduler` 函数中每次在进程进程调度，选择新的进程运行时，增加进程的 `stride` 值：  

```diff
void scheduler()
{
	struct proc *p;
	for (;;) {
		p = fetch_task();
		if (p == NULL) {
			panic("all app are over!\n");
		}
		tracef("swtich to proc %d", p - pool);
		p->state = RUNNING;
		current_proc = p;
+		p->stride += BIG_STRIDE / p->priority;
		swtch(&idle.context, &p->context);
	}
}
```

最后需要编写优先级队列，使得每次取出的进程都是 `stride` 最小的进程。  

传统的优先级队列采用堆实现，但是鉴于本实验的进程数较少，因此采用直接全部扫描找最小值的方式实现。为了方便，直接在原来的 `queue` 基础上更改 `pop_queue` 的逻辑即可。  

由于进程 `proc.c` 是依赖于 `queue.c` 的，基于松耦合原则为了解耦，并且为了实现相对独立且通用的 `queue` 实现，因此在 `queue` 中增加成员 `int (*cmp_func)(int x, int y)` 用于储存队列元素的比较函数。规定当且仅当 `x` 必须在 `y` 后面出队时，该函数返回真。基于此思路，队列结构体的更改以及 `pop_queue` 的逻辑如下：  

```diff
struct queue {
	int data[QUEUE_SIZE];
	int front;
	int tail;
	int empty;
+	int (*cmp_func)(int, int);
};
```

```c
int pop_queue(struct queue *q)
{
	if (q->empty)
		return -1;

	int max_idx = q->front;
    
    // 寻找应当最先出队的元素
	for (int i = (max_idx + 1) % NPROC; i != q->tail; i = (i + 1) % NPROC) {
		if (q->cmp_func(q->data[max_idx], q->data[i])) {
			max_idx = i;
		}
	}

	int val = q->data[max_idx];				// 获取即将出队的元素
	q->data[max_idx] = q->data[q->front];	// 将队头元素补在出队位置
	q->front = (q->front + 1) % NPROC;		// 调整队头指针
	if (q->front == q->tail)				// 队列为空
		q->empty = 1;
	return val;
}
```

同时，在初始化队列的时候，需要加上比较函数的初始化：  

```diff
void init_queue(struct queue *q, int (*cmp_func)(int, int))
{
	q->front = q->tail = 0;
	q->empty = 1;

+	q->cmp_func = cmp_func == NULL ? default_cmp_func : cmp_func;
}
```

其中，`default_cmp_func` 是默认的比较函数，使用小于号比较。  

由于是进程 `stride` 值最小的先出队，因此比较函数返回真，当且仅当前一个进程后出列，当且仅当后一个进程的 `stride` 比前一个进程校。故在 `proc.c` 中的比较函数如下：  

```c
static int proc_cmp_func(int id1, int id2)
{
	return pool[id2].stride < pool[id1].stride; 
}
```

则使用 `init_queue(&task_queue, proc_cmp_func);`   初始化任务队列即可。  

这样便实现了 `stride` 调度算法。  

## 运行结果

运行命令：  

```shell
$ make test BASE=0
```

进入 usershell。在 shell 中运行 ch5_usertest，可以得到如下输出：  

```
Usertests: Running ch2b_hello_world
Usertests: Running ch2b_power
Usertests: Running ch3b_sleep
Usertests: Running ch3b_sleep1
Hello world from user mode program!
Test hello_world OK!
3^10000=5079

...

Test getpid OK! pid = 143, ppid = 58
Test getpid OK! pid = 144, ppid = 58
Test getpid OK! pid = 145, ppid = 58
forktest1 pass.
new child 150
new child 151
Test getppid OK!
Test getppid OK!
Test getpid OK! pid = 150, ppid = 58
Test getpid OK! pid = 151, ppid = 58
Test many spawn OK!
Test sleep OK!
priority = 5, exitcode = 6126400
priority = 8, exitcode = 10670400
priority = 7, exitcode = 9623200
priority = 6, exitcode = 8134800
priority = 9, exitcode = 12587600
priority = 10, exitcode = 14111600
ch5t usertest passed!
ch5 Usertests passed!
Shell: Process 2 exited with code 0
```

可以看到应当通过的测试全部通过，并且无运行到 fail 的测试。  

对于优先级测试，可以看到，优先级与 `exitcode` 基本上是成比例的。  



## 思考题

本实验选用的 `RustSBI` 版本为 `0.1.1`

+ `p1` 的 `stride` 值为 255，`p2` 为 250，当 `p2` 加 10 以后，由于该无符号数只有 8 位，因此 `p2` 的 `stride` 值变为 4。若是采用直接比较数值大小的方法，则 `p2` 的数值反而比 `p1` 小，因此又会使得 `p2` 执行。  

+ 当进程优先级 `>= 2` 时，进程的 `pass` 值 `<= BigStride / 2`。由于每次都是选取 `stride` 值最小的进程运行，且每次运行进程增加的 `stride` 都不超过 `BigStride / 2`，因而 `stride` 值小的进程在加上 `pass` 后比相对其起 `stride` 值大的进程的值还要大超过 `BigStride / 2`。因此存在 `STRIDE_MAX – STRIDE_MIN <= BigStride / 2`。  

+ 基本思路是，首先进行值的直接比较。如果较大与较小差值不超过 `BigStride / 2`，则直接返回结果；如果插值超过了，说明存在溢出问题，则将结果取反。具体代码如下：  

  ```c
  typedef unsigned long long Stride_t;
  const Stride_t BIG_STRIDE = 0xffffffffffffffffULL;
  int cmp(Stride_t a, Stride_t b) {
      int ret;
      Stride_t diff;
      if (a > b) {
          ret = 1;
          diff = a - b;
      } else if (a < b) {
          ret = -1;
          diff = b - a;
      } else {
          return 0;
      }
      if (diff > BIG_STRIDE / 2) return -ret;
      return ret;
  }
  ```

  

