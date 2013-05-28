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
#include "process_accountant.h"
#include "lattop.h"

static const char stp_template[] = "/tmp/lattop-XXXXXX.stp";

struct stap_reader {
	/* must be first */
	struct polled_reader pr;

	FILE *stap_popen;
	enum { STAP_STARTING, STAP_RUNNING } state;

	char *line;
	size_t len;
	
	char stp_filename[sizeof(stp_template)];
};

static int fill_in(int fd, const char *source_filename)
{
	char buf[64*1024]; /* big enough to read and process the complete stp script */
	char *p;
	ssize_t len;
	FILE *source;

	source = fopen(source_filename, "re");
	if (!source) {
		r = -errno;
		fprintf(stderr, "Opening %s: %m\n", source_filename);
		return r;
	}

	len = fread(buf, 1, sizeof(buf), source);
	fclose(source);
	if (len == sizeof(buf)) {
		fprintf(stderr, "File '%s' is too big", source_filename);
		return -EFBIG;
	}
	
	p = buf;
	do {
		char *at, *nextat;
		at = memchr(p, '@', len);
		if (!at)
			break;

		/* skip over non-interesting source */
		len -= at - p;
		p = at;
		/* XXX at+1 */

		assert(len >= 0);

		/* An '@' could be the last char of the file */
		if (len < 1)
			break;

		nextat = memchr(p + 1, '@', len - 1);
		if (!nextat)
			break;

		
	}
}

static int stap_reader_start(struct polled_reader *pr)
{
	struct stap_reader *sr = (struct stap_reader*) pr;
	int fd, r;

	memcpy(sr->stp_filename, stp_template, sizeof(sr->stp_filename));
	fd = mkstemps(sr->stp_filename, 4);
	if (fd < 0) {
		r = -errno;
		perror("Creating a temp file");
		return r;
	}

	r = fill_in(fd, "lat.stp.in");
	close(fd);
	if (r < 0)
		return r;

	/* XXX avoid popen, use manual pipe,fork,exec */
	sr->stap_popen = popen("stap -g lat.stp", "re");

	return sr->stap_popen ? 0 : -errno;
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

	/* XXX I don't think getline (buffered) mixes well with poll().
         * Should use a non-blocking fd and read everything there is to read. */

	/* 1st line - task info */
	n = getline(&r->line, &r->len, r->stap_popen);
	if (n <= 0)
		return -1;

	if (r->state == STAP_STARTING) {
		if (strcmp(r->line, "lat begin\n"))
			return -1;

		r->state = STAP_RUNNING;
		lattop_reader_started(pr);
		return 0;
	}

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

	pa_account_latency(pid, tid, comm, delay, &bt);

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
