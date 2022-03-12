#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "taskdef.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, char *str, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, str, len);
	if (fd != STDOUT)
		return -1;
	for (int i = 0; i < len; ++i) {
		console_putchar(str[i]);
	}
	return len;
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

uint64 sys_gettimeofday(TimeVal *val, int _tz)
{
	uint64 cycle = get_cycle();
	val->sec = cycle / CPU_FREQ;
	val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

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
		ret = sys_write(args[0], (char *)args[1], args[2]);
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
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
