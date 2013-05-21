/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _PROCESS_H
#define _PROCESS_H

#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include "rbtree.h"
#include "back_trace.h"

struct latency_account {
	uint64_t total;
	uint64_t max;
	int count;
};

static inline void la_clear(struct latency_account *la)
{
	la->total = la->max = la->count = 0;
}

static inline void la_init(struct latency_account *la, uint64_t delay)
{
	la->total = delay;
	la->max   = delay;
	la->count = 1;
}

static inline void la_add_delay(struct latency_account *la, uint64_t delay)
{
	la->total += delay;
	if (la->max < delay)
		la->max = delay;
	la->count++;
}

static inline void la_sum_delay(struct latency_account *la,
                                const struct latency_account *other)
{
	la->total += other->total;
	if (la->max < other->max)
		la->max = other->max;
	la->count += other->count;
}

struct bt2la {
	struct rb_node rb_node;
	struct back_trace bt;	/* key in the rb-tree */
	struct latency_account la;
};

struct process {
	struct rb_node rb_node;		/* tree of processes, sorted by pid */
	struct rb_root bt2la_map;	/* this process's latencies, sorted by the backtrace */
	pid_t pid;
	char comm[16];
	struct latency_account summarized;
};

/*
 * Search the rb-tree
 * Returns the node with the same back_trace if it exists.
 * If not, returns NULL, and sets the 'parent' and 'link' pointers to where
 * the newly created node should be put.
 */
static inline struct bt2la *__rb_search_bt2la(struct process *process,
                                              struct back_trace *bt,
                                              struct rb_node **pparent,
					      struct rb_node ***plink)
{
	struct rb_node **p = &process->bt2la_map.rb_node;
	struct rb_node *parent = NULL;
	struct bt2la *bt2la;
	int cmp;

	while (*p) {
		parent = *p;
		bt2la = rb_entry(parent, struct bt2la, rb_node);

		cmp = bt_compare(bt, &bt2la->bt);
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return bt2la;
	}

	*pparent = parent;
	*plink = p;

	return NULL;
}

static inline void process_suffer_latency(struct process *p, uint64_t delay,
                                          struct back_trace *bt)
{
	struct bt2la *item;
	struct rb_node *parent;
	struct rb_node **link;

	item = __rb_search_bt2la(p, bt, &parent, &link);
	if (item) {
		la_add_delay(&item->la, delay);
		return;
	}

	item = malloc(sizeof(struct bt2la));
	item->bt = *bt;
	la_init(&item->la, delay);

	rb_link_node(&item->rb_node, parent, link);
	rb_insert_color(&item->rb_node, &p->bt2la_map);
}

struct process *process_new(pid_t pid, const char comm[16]);
void process_summarize(struct process *p);
void process_dump(struct process *p);
void process_fini(struct process *p);

#endif
