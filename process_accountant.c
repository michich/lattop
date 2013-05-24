/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <stdio.h>

#include "process_accountant.h"

#include "process.h"
#include "rbtree.h"

struct process_accountant {
	struct rb_root processes;
};

struct process_accountant accountant;

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
	pa_delete_rbtree(accountant.processes.rb_node);
	accountant.processes = RB_ROOT;
}

void pa_dump_and_clear(void)
{
	struct rb_node *node;
	struct process *process;
	for (node = rb_first(&accountant.processes); node; node = rb_next(node)) {
		process = rb_entry(node, struct process, rb_node);
		printf("PID: %d  TID: %d  comm: %s\n", process->pid, process->tid, process->comm);
		process_summarize(process);
		process_dump(process);
		printf("\n");
	}
	printf("-\n");

	pa_clear();
}

static struct process *__search_process(pid_t tid, struct rb_node **pparent,
					struct rb_node ***plink)
{
	struct rb_node **p = &accountant.processes.rb_node;
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

	process = __search_process(tid, &parent, &link);
	if (!process) {
		process = process_new(pid, tid, comm);
		rb_link_node(&process->rb_node, parent, link);
		rb_insert_color(&process->rb_node, &accountant.processes);
	}
	process_suffer_latency(process, delay, bt);
}


void pa_init(void)
{
	accountant.processes = RB_ROOT;
}

void pa_fini(void)
{
	pa_clear();
}
