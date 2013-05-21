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
		process_summarize(process);
		process_dump(process);
		printf("\n");
	}
	printf("-\n");
}

void pa_init(struct process_accountant *pa)
{
	pa->processes = RB_ROOT;
}

void pa_fini(struct process_accountant *pa)
{
	pa_clear_all(pa);
}
