#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
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

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{

	if (__builtin_expect(((uint64)val & 0x7) != 0, 0)) {		// Address isn't aligned
		debugf("In sys_gettimeofday: The address of TimeVal is not aligned!");
		return -1;
	}

	uint64 cycle = get_cycle();
	uint64 sec = cycle / CPU_FREQ;
	uint64 usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	struct proc *p = curr_proc();
	TimeVal *pa = (TimeVal *)useraddr(p->pagetable, (uint64)val);

	if (pa == 0) {
		debugf("In sys_gettimeofday: The address of TimeVal is invalid!");
		return -1;
	}

	pa->sec = sec;

	if (__builtin_expect(PGROUNDDOWN((uint64)pa) != PGROUNDDOWN((uint64)pa + sizeof(pa->sec)), 0)) {	// TimeVal spans two pages
		uint64 *pa1 = (uint64 *)useraddr(p->pagetable, (uint64)val + sizeof(val->sec));
		*pa1 = usec;
	} else {
		pa->usec = usec;
	}

	return 0;
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

	if (((uint64)addr & (PAGE_SIZE - 1)) != 0) {	// Not aligned as PAGE_SIZE
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
	pagetable_t pg = curr_proc()->pagetable;

	for (uint64 va = (uint64)addr; va != end; va += PAGE_SIZE) {
		void *pa = kalloc();
		if (pa == 0) {
			debugf("In sys_mmap: Physical memory is not enough!");
			return -1;
		}

		debugf("In mmap: Will map pa: %p to va: %p", pa, va);
		if (mappages(pg, va, PAGE_SIZE, (uint64)pa, (port << 1) | PTE_U) != 0) {
			debugf("In sys_mmap: Memory map failed!");
			return -1;
		}
	}
	return 0;
}

uint64 sys_munmap(void *addr, uint64 len)
{
	if (len == 0) {
		return 0;
	}
	if (len > ((uint64)1 << 30)) {
		debugf("In sys_munmap: Input len: %d is too large!", len);
		return -1;
	}

	if (((uint64)addr & (PAGE_SIZE - 1)) != 0) {	// Not aligned as PAGE_SIZE
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
		debugf("In munmap: Will free pa: %p originally mapped to va: %p", pa, va);
		uvmunmap(pg, va, 1, 1);
	}
	return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
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
	case SYS_mmap:
		ret = sys_mmap((void *)args[0], (uint64)args[1], (int)args[2], (int)args[3], (int)args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap((void *)args[0], (uint64)args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
