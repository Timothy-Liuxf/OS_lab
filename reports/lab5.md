# lab5

---

2019011008

无92 刘雪枫

---

## 目录

[TOC]

## 新增代码

切换到分支 `ch8` 编写代码



### 数据结构

首先修改进程块 `proc`，增加用于检测死锁的成员：

```diff
 struct proc {
 	// ...
	
+	int deadlock_detect_enabled;
+	int mtx_available[LOCK_POOL_SIZE];
+	int mtx_allocation[NTHREAD][LOCK_POOL_SIZE];
+	int mtx_request[NTHREAD][LOCK_POOL_SIZE];
+	int sem_available[LOCK_POOL_SIZE];
+	int sem_allocation[NTHREAD][LOCK_POOL_SIZE];
+	int sem_request[NTHREAD][LOCK_POOL_SIZE];
 };
```

并在初始化进程时将成员初始化：

```diff
 struct proc *allocproc()
 {
 	// ...
 	p->next_condvar_id = 0;
 	// LAB 5: (1) you may initialize your new proc variables here
+	p->deadlock_detect_enabled = 0;
+	memset(p->mtx_available, 0, sizeof(p->mtx_available));
+	memset(p->mtx_allocation, 0, sizeof(p->mtx_allocation));
+	memset(p->mtx_request, 0, sizeof(p->mtx_request));
+	memset(p->sem_available, 0, sizeof(p->sem_available));
+	memset(p->sem_allocation, 0, sizeof(p->sem_allocation));
+	memset(p->sem_request, 0, sizeof(p->sem_request));
+	return p;
 }
```



### 实现 `enable_deadlock_detect` 系统调用

然后在 `syscall` 中捕获 `SYS_enable_deadlock_detect` 调用，使其调用 `sys_enable_deadlock_detect` 函数。函数定义如下：  

```c
int sys_enable_deadlock_detect(int is_enable)
{
	return enable_deadlock_detect(is_enable);
}
```

其中，`enable_deadlock_detect` 定义如下：  

```c
int enable_deadlock_detect(int is_enable)
{
	if (is_enable != 0 && is_enable != 1) {
		return -1;
	}
	curr_proc()->deadlock_detect_enabled = is_enable;
	return 0;
}
```



### 死锁检测算法

下面在 `syscall.c` 中实现死锁检测算法。  

```c
int deadlock_detect(const int available[LOCK_POOL_SIZE],
		    const int allocation[NTHREAD][LOCK_POOL_SIZE],
		    const int request[NTHREAD][LOCK_POOL_SIZE])
{
	int finish[NTHREAD] = { 0 };
	int work[LOCK_POOL_SIZE];
	memcpy(work, available, sizeof(work));

	int has_modified = 0;
	while (1) {
		has_modified = 0;
		for (int i = 0; i < NTHREAD; ++i) {
			if (finish[i] == 0) {
				int can_alloc = 1;
				for (int j = 0; j < LOCK_POOL_SIZE; ++j) {
					if (request[i][j] > work[j]) {
						can_alloc = 0;
						break;
					}
				}
				if (can_alloc) {
					finish[i] = 1;
					for (int j = 0; j < LOCK_POOL_SIZE;
					     ++j) {
						work[j] += allocation[i][j];
					}
					has_modified = 1;
				}
			}
		}
		if (has_modified) {
			for (int i = 0; i < NTHREAD; ++i) {
				if (finish[i] == 0) {
					goto continue_loop;
				}
			}
			return 0; // No deadlock
		} else {
			return -1; // Deadlock
		}
	continue_loop:;
	}
}
```



### 实现 `mutex` 死锁检测

对于 `mutex` ，在创建、锁、解锁的过程中更新维护相关的值，并在加锁前检测死锁：

```diff
 int sys_mutex_create(int blocking)
 {
 	struct mutex *m = mutex_create(blocking);
 	if (m == NULL) {
 		errorf("fail to create mutex: out of resource");
 		return -1;
 	}
 	// LAB5: (4-1) You may want to maintain some variables for detect here
 	int mutex_id = m - curr_proc()->mutex_pool;
+	curr_proc()->mtx_available[mutex_id]++;
 	debugf("create mutex %d", mutex_id);
 	return mutex_id;
}

 int sys_mutex_lock(int mutex_id)
 {
 	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
 		errorf("Unexpected mutex id %d", mutex_id);
 		return -1;
 	}
 	// LAB5: (4-1) You may want to maintain some variables for detect
 	//       or call your detect algorithm here
 
+	struct proc *p = curr_proc();
+	int tid = curr_thread()->tid;
 
+	p->mtx_request[tid][mutex_id]++;
+	if (p->deadlock_detect_enabled &&
+	    deadlock_detect(p->mtx_available, p->mtx_allocation,
+			    p->mtx_request) != 0) {
+		return -0xDEAD;
+	}
 
 	mutex_lock(&curr_proc()->mutex_pool[mutex_id]);
 
+	p->mtx_allocation[tid][mutex_id]++;
+	p->mtx_request[tid][mutex_id]--;
+	p->mtx_available[mutex_id]--;
 
 	return 0;
 }

 int sys_mutex_unlock(int mutex_id)
 {
 	if (mutex_id < 0 || mutex_id >= curr_proc()->next_mutex_id) {
 		errorf("Unexpected mutex id %d", mutex_id);
 		return -1;
 	}
 	// LAB5: (4-1) You may want to maintain some variables for detect here
 
+	struct proc *p = curr_proc();
+	int tid = curr_thread()->tid;
+	p->mtx_allocation[tid][mutex_id]--;
+	p->mtx_available[mutex_id]++;
 
 	mutex_unlock(&curr_proc()->mutex_pool[mutex_id]);
 
 	return 0;
 }
```



### 实现 `semaphore` 死锁检测

该死锁检测是与 `mutex` 是类似的。

```diff
 int sys_semaphore_create(int res_count)
 {
 	struct semaphore *s = semaphore_create(res_count);
 	if (s == NULL) {
 		errorf("fail to create semaphore: out of resource");
 		return -1;
 	}
 	// LAB5: (4-2) You may want to maintain some variables for detect here
 	int sem_id = s - curr_proc()->semaphore_pool;
+	curr_proc()->sem_available[sem_id] = res_count;
 	debugf("create semaphore %d", sem_id);
 	return sem_id;
 }
 
 int sys_semaphore_up(int semaphore_id)
 {
 	if (semaphore_id < 0 ||
 	    semaphore_id >= curr_proc()->next_semaphore_id) {
 		errorf("Unexpected semaphore id %d", semaphore_id);
 		return -1;
 	}
 	// LAB5: (4-2) You may want to maintain some variables for detect here
+	struct proc *p = curr_proc();
+	int tid = curr_thread()->tid;
+	p->sem_allocation[tid][semaphore_id]--;
+	p->sem_available[semaphore_id]++;
 
 	semaphore_up(&curr_proc()->semaphore_pool[semaphore_id]);
 	return 0;
 }
 
 int sys_semaphore_down(int semaphore_id)
 {
 	if (semaphore_id < 0 ||
 	    semaphore_id >= curr_proc()->next_semaphore_id) {
 		errorf("Unexpected semaphore id %d", semaphore_id);
 		return -1;
 	}
 	// LAB5: (4-2) You may want to maintain some variables for detect
 	//       or call your detect algorithm here
 
+	struct proc *p = curr_proc();
+	int tid = curr_thread()->tid;
 
+	p->sem_request[tid][semaphore_id]++;
+	if (p->deadlock_detect_enabled &&
+	    deadlock_detect(p->sem_available, p->sem_allocation,
+			    p->sem_request) != 0) {
+		return -0xDEAD;
+	}
 
 	semaphore_down(&curr_proc()->semaphore_pool[semaphore_id]);
 
+	p->sem_allocation[tid][semaphore_id]++;
+	p->sem_request[tid][semaphore_id]--;
+	p->sem_available[semaphore_id]--;
 
 	return 0;
 }
```





## 运行结果

运行命令：  

```shell
$ make test BASE=0 CHAPTER=8 INIT_PROC=ch8_usertest
```

在最后可以看到：  

```
ch8 Usertests passed!
```

可以看到程序正常运行，说明编写的代码正确  



## 思考题

### 1

*在我们的多线程实现中，当主线程 (即 0 号线程) 退出时，视为整个进程退出， 此时需要结束该进程管理的所有线程并回收其资源。*

+ *需要回收的资源有哪些？*

  需要回收子线程、页表、打开的文件、信号量、互斥量、条件变量，等等。

+ *其他线程的 `struct thread` 可能在哪些位置被引用，分别是否需要回收，为什么？*

  其他线程的 `struct thread` 可能在任务队列中，也可能在信号量、互斥量、条件变量的等待队列中，等等。这些是不需要回收的，因为线程的状态会被置为 `UNUSED`，在任务队列中不会被调度；且进程已经失效，信号量、互斥量、条件变量也不会再被使用，因此也不需要回收。

### 2

两种 `mutex_unlock` 区别是，前者只要锁被锁住，则一定会将锁的状态置为零；而后者只有当没有任务在等待锁的时候才会置为零。对于前者的实现，即使有阻塞任务被放到待调度队列里，也有可能在调度之前被优先级高的进程抢占到锁；而对于后者的实现，在等待队列 pop 出的进程是直接持有锁的，此时即使有优先级高的进程想要去锁，也抢占不到锁，因此后者的实现是先进先出的，即先进入到锁的等待队列的进程会被先分配到锁，但是前者不是这样。 

 

