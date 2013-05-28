/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "process_accountant.h"

#include "lattop.h"
#include "process.h"
#include "rbtree.h"

static struct rb_root processes;
static unsigned count;

static void pa_delete_rbtree(struct rb_node *n)
{
	struct process *p;
	if (!n)
		return;
	pa_delete_rbtree(n->rb_left);
	pa_delete_rbtree(n->rb_right);
	p = rb_entry(n, struct process, rb_node);
	process_fini(p);
	free(p);
}

void pa_clear(void)
{
	pa_delete_rbtree(processes.rb_node);
	processes = RB_ROOT;
	count = 0;
}

static int compare_by_max_latency(const void *p1, const void *p2)
{
	struct process *pr1 = *(struct process**)p1;
	struct process *pr2 = *(struct process**)p2;

	if (pr1->summarized.max < pr2->summarized.max)
		return 1;
	else if (pr1->summarized.max > pr2->summarized.max)
		return -1;
	else
		return 0;
}

static int compare_by_total_latency(const void *p1, const void *p2)
{
	struct process *pr1 = *(struct process**)p1;
	struct process *pr2 = *(struct process**)p2;

	if (pr1->summarized.total < pr2->summarized.total)
		return 1;
	else if (pr1->summarized.total > pr2->summarized.total)
		return -1;
	else
		return 0;
}

static int compare_by_pid(const void *p1, const void *p2)
{
	struct process *pr1 = *(struct process**)p1;
	struct process *pr2 = *(struct process**)p2;

	if (pr1->pid < pr2->pid)
		return -1;
	else if (pr1->pid > pr2->pid)
		return 1;
	else if (pr1->tid < pr2->tid)
		return -1;
	else if (pr1->tid > pr2->tid)
		return 1;
	else
		return 0;
}

void pa_dump_and_clear(void)
{
	struct rb_node *node;
	struct process *process, **array;
	time_t cur_time;
	unsigned n = 0;

	static int (*const sort_func[_NR_SORT_BY])(const void *, const void *) = {
		[SORT_BY_MAX_LATENCY]   = compare_by_max_latency,
		[SORT_BY_TOTAL_LATENCY] = compare_by_total_latency,
		[SORT_BY_PID]           = compare_by_pid,
	};

	time(&cur_time);

	array = alloca(sizeof(struct process*) * count);

	/* summarize processes */
	for (node = rb_first(&processes); node; node = rb_next(node)) {
		process = rb_entry(node, struct process, rb_node);
		process_summarize(process);
		array[n++] = process;
	}

	/* sort by whatever key */
	assert(n == count);
	qsort(array, count, sizeof(struct process*), sort_func[arg_sort]);

	/* dump processes */
	putchar('\n');
	if (!arg_reverse)
		for (n = 0; n < count; n++)
			process_dump(array[n]);
	else
		for (n = count; n > 0; n--)
			process_dump(array[n-1]);
	printf("=== %s", ctime(&cur_time));

	pa_clear();
}

static struct process *search_process(pid_t tid, struct rb_node **pparent,
				      struct rb_node ***plink)
{
	struct rb_node **p = &processes.rb_node;
	struct rb_node *parent = NULL;
	struct process *process;

	while (*p) {
		parent = *p;
		process = rb_entry(parent, struct process, rb_node);

		if (tid < process->tid)
			p = &(*p)->rb_left;
		else if (tid > process->tid)
			p = &(*p)->rb_right;
		else
			return process;
	}

	*pparent = parent;
	*plink = p;

	return NULL;
}

void pa_account_latency(pid_t pid, pid_t tid, const char comm[16], uint64_t delay,
                        struct back_trace *bt)
{
	struct process *process;
	struct rb_node *parent;
	struct rb_node **link;

	process = search_process(tid, &parent, &link);
	if (!process) {
		process = process_new(pid, tid, comm);
		rb_link_node(&process->rb_node, parent, link);
		rb_insert_color(&process->rb_node, &processes);
		count++;
	}
	process_suffer_latency(process, delay, bt);
}


void pa_init(void)
{
	processes = RB_ROOT;
}

void pa_fini(void)
{
	pa_clear();
}
