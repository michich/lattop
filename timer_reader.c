/*
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#include <sys/timerfd.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "timer_reader.h"

#include "process_accountant.h"

struct timer_reader {
	/* must be first */
	struct polled_reader pr;

	int timerfd;

	int interval;
	int count;
};

static int timer_reader_start(struct polled_reader *pr)
{
	struct timer_reader *tr = (struct timer_reader*) pr;
	const struct itimerspec its = {
		.it_interval = { tr->interval, 0 },
		.it_value =    { tr->interval, 0 },
	};
	int r;

	tr->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (tr->timerfd < 0)
		return -errno;

	r = timerfd_settime(tr->timerfd, 0, &its, NULL);
	if (r < 0) {
		r = -errno;
		close(tr->timerfd);
		return r;
	}

	return 0;
}

static int timer_reader_handle_ready_fd(struct polled_reader *pr)
{
	struct timer_reader *tr = (struct timer_reader*) pr;
	uint64_t expired;
	ssize_t r;

	r = read(tr->timerfd, &expired, sizeof(uint64_t));
	if (r != sizeof(uint64_t)) {
		fprintf(stderr, "Invalid read from timerfd\n");
		return -1;
	}

	pa_dump_and_clear();

	if (tr->count <= 0)  /* run indefinitely */
		return 0;

	if (--tr->count == 0)
		return 1;    /* game over */

	return 0;
}

static void timer_reader_fini(struct polled_reader *pr)
{
	struct timer_reader *tr = (struct timer_reader*) pr;
	close(tr->timerfd);
}

static int timer_reader_get_fd(struct polled_reader *pr)
{
	struct timer_reader *r = (struct timer_reader*) pr;
	return r->timerfd;
}

static const struct polled_reader_ops timer_reader_ops = {
	.fini = timer_reader_fini,
	.start = timer_reader_start,
	.get_fd = timer_reader_get_fd,
	.handle_ready_fd = timer_reader_handle_ready_fd,
};

struct polled_reader *timer_reader_new(int interval, int count)
{
	struct timer_reader *r;

	r = calloc(1, sizeof(struct timer_reader));
	if (r == NULL)
		return NULL;

	r->pr.ops = &timer_reader_ops;

	r->interval = interval;
	r->count = count;

	return &r->pr;
}
