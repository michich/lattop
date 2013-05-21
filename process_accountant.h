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
void pa_account_latency(struct process_accountant *pa, pid_t pid,
                        const char comm[16], uint64_t delay,
                        struct back_trace *bt);

#endif
