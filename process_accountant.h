/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _PROCESS_ACCOUNTANT_H
#define _PROCESS_ACCOUNTANT_H

#include "process.h"

struct process_accountant {
	struct rb_root processes;
};

void pa_init(struct process_accountant *pa);
void pa_fini(struct process_accountant *pa);
void pa_summarize(struct process_accountant *pa);
void pa_dump(struct process_accountant *pa);
void pa_clear_all(struct process_accountant *pa);

static inline struct process *__search_process(struct process_accountant *pa,
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

static inline void pa_account_latency(struct process_accountant *pa, pid_t pid,
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

#endif
