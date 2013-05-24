/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _PROCESS_ACCOUNTANT_H
#define _PROCESS_ACCOUNTANT_H

#include <sys/types.h>
#include <stdint.h>

#include "back_trace.h"

void pa_init(void);
void pa_fini(void);
void pa_summarize(void);
void pa_dump(void);
void pa_clear_all(void);
void pa_account_latency(pid_t pid, const char comm[16], uint64_t delay,
                        struct back_trace *bt);

#endif
