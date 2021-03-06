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

void pa_account_latency(pid_t pid, pid_t tid, const char comm[16],
                        uint64_t delay, struct back_trace *bt);
void pa_dump_and_clear(void);

#endif
