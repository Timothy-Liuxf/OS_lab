#include "queue.h"
#include "defs.h"

static int default_cmp_func(int x, int y) {
	return x < y;
}

void init_queue(struct queue *q, int (*cmp_func)(int, int))
{
	q->front = q->tail = 0;
	q->empty = 1;

	q->cmp_func = cmp_func == NULL ? default_cmp_func : cmp_func;
}

void push_queue(struct queue *q, int value)
{
	if (!q->empty && q->front == q->tail) {
		panic("queue shouldn't be overflow");
	}
	q->empty = 0;
	q->data[q->tail] = value;
	q->tail = (q->tail + 1) % NPROC;
}

int pop_queue(struct queue *q)
{
	if (q->empty)
		return -1;

	// int value = q->data[q->front];
	// q->front = (q->front + 1) % NPROC;
	// if (q->front == q->tail)
	// 	q->empty = 1;
	// return value;

	int max_idx = q->front;
	for (int i = (max_idx + 1) % NPROC; i != q->tail; i = (i + 1) % NPROC) {
		if (q->cmp_func(q->data[max_idx], q->data[i])) {
			max_idx = i;
		}
	}

	int val = q->data[max_idx];
	q->data[max_idx] = q->data[q->front];
	q->front = (q->front + 1) % NPROC;
	if (q->front == q->tail)
		q->empty = 1;
	return val;
}
