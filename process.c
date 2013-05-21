/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <stdio.h>
#include "process.h"

void process_dump(struct process *p)
{
	struct rb_node *node;
	for (node = rb_first(&p->bt2la_map); node; node = rb_next(node)) {
		struct bt2la *bt2la = rb_entry(node, struct bt2la, rb_node);
		printf(" Trace: "); bt_dump(&bt2la->bt); printf("\n");
		printf(" Total: %lu\n", bt2la->la.total);
		printf(" Max  : %lu\n", bt2la->la.max);
		printf(" Count: %d\n",  bt2la->la.count);
	}

	printf("Total: %lu\n", p->summarized.total);
	printf("Max  : %lu\n", p->summarized.max);
	printf("Count: %d\n",  p->summarized.count);
}

void process_summarize(struct process *p)
{
	struct rb_node *node;

	la_clear(&p->summarized);
	for (node = rb_first(&p->bt2la_map); node; node = rb_next(node)) {
		struct bt2la *bt2la = rb_entry(node, struct bt2la, rb_node);
		la_sum_delay(&p->summarized, &bt2la->la);
	}
}

struct process *process_new(pid_t pid)
{
	struct process *p;
	p = malloc(sizeof(struct process));
	if (!p)
		return NULL;
	p->bt2la_map = RB_ROOT;
	p->pid = pid;
	la_clear(&p->summarized);
	return p;
}

/*
 * rb_erase would be an overkill. There's no need to rebalance,
 * since we're destroying the whole tree.
 */
static void process_delete_rbtree(struct rb_node *n)
{
	struct bt2la *item;
	if (!n)
		return;
	process_delete_rbtree(n->rb_left);
	process_delete_rbtree(n->rb_right);
	item = rb_entry(n, struct bt2la, rb_node);
	free(item);
}

void process_fini(struct process *p)
{
	process_delete_rbtree(p->bt2la_map.rb_node);
}
