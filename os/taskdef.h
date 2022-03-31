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
