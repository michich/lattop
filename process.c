/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "process.h"

#include "timespan.h"
#include "lat_translator.h"
#include "lattop.h"

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

static int compare_by_max_latency(const void *p1, const void *p2)
{
	struct bt2la *b1 = *(struct bt2la**)p1;
	struct bt2la *b2 = *(struct bt2la**)p2;

	if (b1->la.max < b2->la.max)
		return 1;
	else if (b1->la.max > b2->la.max)
		return -1;
	else
		return 0;
}

static int compare_by_total_latency(const void *p1, const void *p2)
{
	struct bt2la *b1 = *(struct bt2la**)p1;
	struct bt2la *b2 = *(struct bt2la**)p2;

	if (b1->la.total < b2->la.total)
		return 1;
	else if (b1->la.total > b2->la.total)
		return -1;
	else
		return 0;
}

void process_dump(struct process *p)
{
	struct rb_node *node;
	struct bt2la **array;
	char sym_bt[1000], commpidtid[45], total[32], max[32];
	unsigned n = 0;

	static int (*const sort_func[_NR_SORT_BY])(const void *, const void *) = {
		[SORT_BY_MAX_LATENCY]   = compare_by_max_latency,
		[SORT_BY_TOTAL_LATENCY] = compare_by_total_latency,
		[SORT_BY_PID]           = compare_by_max_latency, /* sorting by pid makes no sense within a process */
	};

	format_timespan(total, 32, p->summarized.total/1000, 0);
	format_ms(max,   32, p->summarized.max/1000, 3);

	if (p->pid != p->tid)
		snprintf(commpidtid, sizeof(commpidtid), "%s (%d, thread %d)", p->comm, p->pid, p->tid);
	else
		snprintf(commpidtid, sizeof(commpidtid), "%s (%d)", p->comm, p->pid);

	printf("%-44s Max:%8s Total:%8s\n", commpidtid, max, total);

	array = alloca(sizeof(struct bt2la*) * p->bt2la_count);

	for (node = rb_first(&p->bt2la_map); node; node = rb_next(node)) {
		struct bt2la *bt2la = rb_entry(node, struct bt2la, rb_node);
		array[n++] = bt2la;
	}

	assert(n == p->bt2la_count);
	qsort(array, n, sizeof(struct bt2la*), sort_func[arg_sort]);

	for (n = 0; n < p->bt2la_count; n++) {
		struct bt2la *bt2la = array[!arg_reverse ? n : p->bt2la_count - n - 1];
		double percentage = (bt2la->la.total*100.0)/p->summarized.total;
		const char *translation;

		bt_save_symbolic(&bt2la->bt, sym_bt+1, sizeof(sym_bt)-1);
		translation = lat_translator_translate_stack(sym_bt+1);
		if (!translation) {
			size_t end = strnlen(sym_bt+1, 42);
			sym_bt[0] = '[';
			/* this is safe, because sym_bt array is way larger than our strnlen limit above */
			sym_bt[end+1] = ']';
			sym_bt[end+2] = '\0';
		}


		format_ms(total, 32, bt2la->la.total/1000, 3);
		format_ms(max,   32, bt2la->la.max/1000, 3);

		printf(" %-44s Max:%8s %5.1f%%\n", translation ?: sym_bt, max, percentage);
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
	p->bt2la_count++;
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
	p->bt2la_count = 0;
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
