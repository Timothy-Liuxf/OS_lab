#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "taskdef.h"
#include "timer.h"
#include "trap.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(
	TimeVal *val,
	int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	struct proc *p = curr_proc();
	// TimeVal *pa = (TimeVal *)useraddr(p->pagetable, (uint64)val);

	// if (pa == 0) {
	// 	debugf("In sys_gettimeofday: The address of TimeVal is invalid!");
	// 	return -1;
	// }

	TimeVal val_impl;
	uint64 cycle = get_cycle();
	val_impl.sec = cycle / CPU_FREQ;
	val_impl.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	return copyout(p->pagetable, (uint64)val, (char *)&val_impl,
		       sizeof(*val));
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

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

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 path, uint64 uargv)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	debugf("sys_spawn %s\n", name);
	return spawn(name, (char **)argv);
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)

uint64 sys_mmap(void *addr, uint64 len, int port, int flag, int fd)
{
	// Unused parameter
	(void)flag;
	(void)fd;

	if (len == 0) {
		return 0;
	}
	if (len > ((uint64)1 << 30)) {
		debugf("In sys_mmap: Input len: %d is too large!", len);
		return -1;
	}

	if (((uint64)addr & (PAGE_SIZE - 1)) != 0) { // Not aligned as PAGE_SIZE
		debugf("In sys_mmap: Input addr is not aligned as PAGE_SIZE!");
		return -1;
	}

	if ((port & ~0x7) != 0) {
		debugf("In sys_mmap: High bits of input port are not zero!");
		return -1;
	}

	if ((port & 0x7) == 0) {
		debugf("In sys_mmap: Loweast 3 bits of input port are all zero!");
		return -1;
	}

	len = PGROUNDUP(len);
	uint64 end = (uint64)addr + len;
	struct proc *p = curr_proc();
	pagetable_t pg = p->pagetable;
	uint64 va;
	uint64 ret_val = 0;

	for (va = (uint64)addr; va != end; va += PAGE_SIZE) {
		void *pa = kalloc();
		if (pa == 0) {
			debugf("In sys_mmap: Physical memory is not enough!");
			ret_val = -1;
			goto ret;
		}

		debugf("In mmap: Will map pa: %p to va: %p", pa, va);
		if (mappages(pg, va, PAGE_SIZE, (uint64)pa,
			     (port << 1) | PTE_U) != 0) {
			debugf("In sys_mmap: Memory map failed!");
			ret_val = -1;
			goto ret;
		}
	}

	ret_val = 0;
ret:;
	uint64 new_max_page = va / PAGE_SIZE;
	if (new_max_page > p->max_page) {
		p->max_page = new_max_page;
	}
	return ret_val;
}

uint64 sys_munmap(void *addr, uint64 len)
{
	debugf("In munmap: Wants to unmmap: va: %p len: %d", addr, len);

	if (len == 0) {
		return 0;
	}
	if (len > ((uint64)1 << 30)) {
		debugf("In sys_munmap: Input len: %d is too large!", len);
		return -1;
	}

	if (((uint64)addr & (PAGE_SIZE - 1)) != 0) { // Not aligned as PAGE_SIZE
		debugf("In sys_munmap: Input addr is not aligned as PAGE_SIZE!");
		return -1;
	}

	len = PGROUNDUP(len);
	uint64 end = (uint64)addr + len;
	pagetable_t pg = curr_proc()->pagetable;

	for (uint64 va = (uint64)addr; va != end; va += PAGE_SIZE) {
		uint64 pa = walkaddr(pg, va);
		if (pa == 0) {
			debugf("In sys_munmap: One page is not mapped!");
			return -1;
		}
		debugf("In munmap: Will free pa: %p originally mapped to va: %p",
		       pa, va);
		uvmunmap(pg, va, 1, 1);
	}
	return 0;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	memset(path, 0, sizeof(path));
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_fstat(int fd, uint64 stat)
{
	// TODO: your job is to complete the syscall
	return -1;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath,
	       uint64 flags)
{
	// TODO: your job is to complete the syscall
	if (oldpath == 0 || newpath == 0) {
		return -1;
	}
	struct proc *p = curr_proc();
	char oldpathbuf[DIRSIZ], newpathbuf[DIRSIZ];
	memset((void *)oldpathbuf, 0, sizeof(oldpathbuf));
	memset((void *)newpathbuf, 0, sizeof(newpathbuf));
	copyinstr(p->pagetable, oldpathbuf, oldpath, DIRSIZ);
	copyinstr(p->pagetable, newpathbuf, oldpath, DIRSIZ);
	return linkat(olddirfd, oldpathbuf, newdirfd, newpathbuf, flags);
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
	struct proc *p = curr_proc();
	char namebuf[DIRSIZ];
	memset(namebuf, 0, sizeof(namebuf));
	copyinstr(p->pagetable, namebuf, name, DIRSIZ);
	return unlinkat(dirfd, namebuf, flags);
}

uint64 sys_set_priority(long long prio)
{
	return setpriority(prio);
}

extern char trap_page[];

void syscall()
{
	struct proc *proc_ptr = curr_proc();
	struct trapframe *trapframe = proc_ptr->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	if (id < MAX_SYSCALL_NUM) {
		++proc_ptr->syscall_times[id];
	}

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], args[1]);
		break;
	case SYS_linkat:
		ret = sys_linkat(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_unlinkat:
		ret = sys_unlinkat(args[0], args[1], args[2]);
	case SYS_spawn:
		ret = sys_spawn(args[0], args[1]);
		break;
	case SYS_mmap:
		ret = sys_mmap((void *)args[0], (uint64)args[1], (int)args[2],
			       (int)args[3], (int)args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap((void *)args[0], (uint64)args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
