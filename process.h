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

void process_suffer_latency(struct process *p, uint64_t delay,
                            struct back_trace *bt);
struct process *process_new(pid_t pid, const char comm[16]);
void process_summarize(struct process *p);
void process_dump(struct process *p);
void process_fini(struct process *p);

#endif
