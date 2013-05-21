/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <stdio.h>
#include "process_accountant.h"

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

void pa_clear_all(struct process_accountant *pa)
{
	pa_delete_rbtree(pa->processes.rb_node);
	pa->processes = RB_ROOT;
}

void pa_dump(struct process_accountant *pa)
{
	struct rb_node *node;
	struct process *process;
	for (node = rb_first(&pa->processes); node; node = rb_next(node)) {
		process = rb_entry(node, struct process, rb_node);
		printf("PID: %d\n", process->pid);
		printf("comm: %s\n", process->comm);
		process_summarize(process);
		process_dump(process);
		printf("\n");
	}
	printf("-\n");
}

static struct process *__search_process(struct process_accountant *pa,
                                               pid_t pid,
                                               struct rb_node **pparent,
					       struct rb_node ***plink)
{
	struct rb_node **p = &pa->processes.rb_node;
	struct rb_node *parent = NULL;
	struct process *process;

	while (*p) {
		parent = *p;
		process = rb_entry(parent, struct process, rb_node);

		if (pid < process->pid)
			p = &(*p)->rb_left;
		else if (pid > process->pid)
			p = &(*p)->rb_right;
		else
			return process;
	}

	*pparent = parent;
	*plink = p;

	return NULL;
}

void pa_account_latency(struct process_accountant *pa, pid_t pid,
                        const char comm[16], uint64_t delay,
                        struct back_trace *bt)
{
	struct process *process;
	struct rb_node *parent;
	struct rb_node **link;

	process = __search_process(pa, pid, &parent, &link);
	if (!process) {
		process = process_new(pid, comm);
		rb_link_node(&process->rb_node, parent, link);
		rb_insert_color(&process->rb_node, &pa->processes);
	}
	process_suffer_latency(process, delay, bt);
}


void pa_init(struct process_accountant *pa)
{
	pa->processes = RB_ROOT;
}

void pa_fini(struct process_accountant *pa)
{
	pa_clear_all(pa);
}
