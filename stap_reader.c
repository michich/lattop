/*
 * stap_reader reads stack traces of waking processes using a SystemTap script
 *
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stap_reader.h"
#include "back_trace.h"
#include "lattop.h"

static int stap_reader_start(struct polled_reader *pr)
{
	struct stap_reader *r = (struct stap_reader*) pr;

	/* TODO avoid popen, use manual pipe,fork,exec */
	r->stap_popen = popen("stap -g lat.stp", "re");

	return r->stap_popen ? 0 : -errno;
}

static int stap_reader_handle_ready_fd(struct polled_reader *pr)
{
	struct stap_reader *r = (struct stap_reader*) pr;
	ssize_t n;
	char *str;
	char comm[16]; /* TASK_COMM_LEN */
	unsigned long delay;
	unsigned long pid;
	unsigned long tid;
	struct back_trace bt;
	int depth;
	int nread;
	char sleep_or_block;
	bool end_of_trace;

	/* TODO I don't think getline (buffered) mixes well with poll().
         * Should use a non-blocking fd and read everything there is to read. */

	/* 1st line - task info */
	n = getline(&r->line, &r->len, r->stap_popen);
	if (n <= 0)
		return -1;

	if (sscanf(r->line, "%c %lu %lu %lu %15[^\n]", &sleep_or_block, &delay, &pid, &tid, comm) != 5)
		return -1;

	/* 2nd line - stack trace */
	n = getline(&r->line, &r->len, r->stap_popen);
	if (n <= 0)
		return -1;

	str = r->line;
	end_of_trace = false;
	for (depth = 0; depth < MAX_BT_LEN; depth++) {
		if (end_of_trace) {
			bt.trace[depth] = 0;
			continue;
		}

		if (sscanf(str, "%lx%n", &bt.trace[depth], &nread) == 1)
			str += nread;
		else {
			bt.trace[depth] = 0;
			end_of_trace = true;
		}
	}

	pa_account_latency(app_getPA(), pid, comm, delay, &bt);

	return 0;
}

static void stap_reader_fini(struct polled_reader *pr)
{
	struct stap_reader *r = (struct stap_reader*) pr;
	if (r->stap_popen)
		pclose(r->stap_popen);
	free(r->line);
}

static int stap_reader_get_fd(struct polled_reader *pr)
{
	struct stap_reader *r = (struct stap_reader*) pr;
	return fileno(r->stap_popen);
}

static const struct polled_reader_ops stap_reader_ops = {
	.fini = stap_reader_fini,
	.start = stap_reader_start,
	.get_fd = stap_reader_get_fd,
	.handle_ready_fd = stap_reader_handle_ready_fd,
};

struct polled_reader *stap_reader_new(void)
{
	struct stap_reader *r;

	r = calloc(1, sizeof(struct stap_reader));
	if (r == NULL)
		return NULL;

	r->pr.ops = &stap_reader_ops;

	return &r->pr;
}
