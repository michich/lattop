/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "process.h"

#include "timespan.h"
#include "lat_translator.h"

static void la_clear(struct latency_account *la)
{
	la->total = la->max = la->count = 0;
}

static void la_init(struct latency_account *la, uint64_t delay)
{
	la->total = delay;
	la->max   = delay;
	la->count = 1;
}

static void la_add_delay(struct latency_account *la, uint64_t delay)
{
	la->total += delay;
	if (la->max < delay)
		la->max = delay;
	la->count++;
}

static void la_sum_delay(struct latency_account *la,
                                const struct latency_account *other)
{
	la->total += other->total;
	if (la->max < other->max)
		la->max = other->max;
	la->count += other->count;
}

void process_dump(struct process *p)
{
	struct rb_node *node;
	char sym_bt[1000], commpidtid[32], total[32], max[32], avg[32];

	format_ms(total, 32, p->summarized.total/1000);
	format_ms(max,   32, p->summarized.max/1000);

	if (p->pid != p->tid)
		sprintf(commpidtid, "%s (%d, thread %d)", p->comm, p->pid, p->tid);
	else
		sprintf(commpidtid, "%s (%d)", p->comm, p->pid);

	printf("%-40s Total: %s\n", commpidtid, total);

	for (node = rb_first(&p->bt2la_map); node; node = rb_next(node)) {
		struct bt2la *bt2la = rb_entry(node, struct bt2la, rb_node);
		double percentage = (bt2la->la.total*100.0)/p->summarized.total;
		const char *translation;

		bt_save_symbolic(&bt2la->bt, sym_bt+1, sizeof(sym_bt)-1);
		translation = lat_translator_translate_stack(sym_bt+1);
		if (!translation) {
			/* XXX safety */
			size_t end = strcspn(sym_bt+1, " ");
			sym_bt[0] = '[';
			sym_bt[end+1] = ']';
			sym_bt[end+2] = '\0';
		}


		format_ms(total, 32, bt2la->la.total/1000);
		format_ms(max,   32, bt2la->la.max/1000);
		format_ms(avg,  32, bt2la->la.total/bt2la->la.count/1000);

		printf("%-45s %5.1f%% Max:%8s Avg:%8s\n", translation ?: sym_bt, percentage, max, avg);
	}
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

/*
 * Search the rb-tree
 * Returns the node with the same back_trace if it exists.
 * If not, returns NULL, and sets the 'parent' and 'link' pointers to where
 * the newly created node should be put.
 */
static struct bt2la *rb_search_bt2la(struct process *process,
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

void process_suffer_latency(struct process *p, uint64_t delay,
                            struct back_trace *bt)
{
	struct bt2la *item;
	struct rb_node *parent;
	struct rb_node **link;

	item = rb_search_bt2la(p, bt, &parent, &link);
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


struct process *process_new(pid_t pid, pid_t tid, const char comm[16])
{
	struct process *p;
	p = malloc(sizeof(struct process));
	if (!p)
		return NULL;
	p->bt2la_map = RB_ROOT;
	p->pid = pid;
	p->tid = tid;
	strcpy(p->comm, comm);
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
