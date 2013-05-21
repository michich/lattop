/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#ifndef _PERF_READER_H
#define _PERF_READER_H

#include "polled_reader.h"
#include "cpumap.h"

struct sample_event;

struct mmap_data {
	void			*base;
	int			mask;
	unsigned int		prev;
};

struct perf_reader {
	/* must be first */
	struct polled_reader pr;

	struct mmap_data md;
	int fds[MAX_NR_CPUS]; /* perf event fds */
	int nr_cpus;
};

struct polled_reader *perf_reader_new(void);

#endif
