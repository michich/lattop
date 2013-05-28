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

#define ELEMENTSOF(x) (sizeof(x)/sizeof((x)[0]))

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

static const struct {
	const char *pattern;
	size_t pattern_len;
	unsigned long long *pvar;
} subst_table[] = {
	{ "@MIN_DELAY@",               sizeof("@MIN_DELAY@")-1,               &arg_min_delay },
	{ "@MAX_INTERRUPTIBLE_DELAY@", sizeof("@MAX_INTERRUPTIBLE_DELAY@")-1, &arg_max_interruptible_delay },
};

static int substitute(char buf[], size_t size, size_t *plen)
{
	char *p, *atsign;
	size_t rem = *plen;
	int i;

	p = buf;
	atsign = memchr(p, '@', rem);

	while (atsign) {
		/* skip over non-interesting source */
		rem -= atsign - p;
		p = atsign;

		assert(rem >= 0);

		/* look for known patterns */
		for (i = 0; i < ELEMENTSOF(subst_table); i++) {
			if (subst_table[i].pattern_len > rem)
				continue;

			if (!memcmp(p, subst_table[i].pattern, subst_table[i].pattern_len))
				break;
		}

		/* found a match? */
		if (i < ELEMENTSOF(subst_table)) {
			char number[32];
			size_t number_len;
			number_len = sprintf(number, "%llu", *subst_table[i].pvar);

			/* substitution may enlarge the result by too much */
			if (p + rem + number_len - subst_table[i].pattern_len > buf + size)
				return -EFBIG;

			/* do the actual substitution */
			memmove(p + number_len, p + subst_table[i].pattern_len, rem - subst_table[i].pattern_len);
			p = mempcpy(p, number, number_len);
			rem -= subst_table[i].pattern_len;
			*plen += number_len - subst_table[i].pattern_len;
		} else {
			p++;
			rem--;
		}

		assert(rem >= 0);

		atsign = memchr(p, '@', rem);
	}

	return 0;
}

static int fill_in(struct stap_reader *sr, int fd, const char *source_filename)
{
	char buf[64*1024]; /* big enough to read and process the complete stp script */
	ssize_t rlen, wlen;
	size_t len;
	int source_fd;
	int r;

	source_fd = open(source_filename, O_RDONLY|O_CLOEXEC);
	if (source_fd < 0) {
		r = -errno;
		fprintf(stderr, "Opening %s: %m\n", source_filename);
		return r;
	}

	rlen = read(source_fd, buf, sizeof(buf));
	if (rlen < 0) {
		r = -errno;
		fprintf(stderr, "Reading from '%s': %m\n", source_filename);
		close(source_fd);
		return r;
	}
	len = rlen;
	if (len == sizeof(buf)) {
		fprintf(stderr, "File '%s' is too big", source_filename);
		close(source_fd);
		return -EFBIG;
	}

	close(source_fd);

	r = substitute(buf, sizeof(buf), &len);
	if (r < 0)
		return r;

	wlen = write(fd, buf, len);
	if (wlen < 0) {
		r = -errno;
		fprintf(stderr, "Writing to '%s': %m\n", sr->stp_filename);
		return r;
	}
	if (wlen != len) {
		fprintf(stderr, "Short write to '%s'\n", sr->stp_filename);
		return -EIO;
	}
	return 0;
}

static int stap_reader_start(struct polled_reader *pr)
{
#define CMD "stap -g "
	char cmd[sizeof(CMD)-1 + sizeof(stp_template)];
	struct stap_reader *sr = (struct stap_reader*) pr;
	int fd, r;

	memcpy(sr->stp_filename, stp_template, sizeof(sr->stp_filename));
	fd = mkstemps(sr->stp_filename, 4);
	if (fd < 0) {
		r = -errno;
		perror("Creating a temp file");
		return r;
	}

	r = fill_in(sr, fd, "lat.stp.in");
	close(fd);
	if (r < 0)
		return r;

	/* XXX avoid popen, use manual pipe,fork,exec */
	memcpy(cmd, CMD, sizeof(CMD)-1);
	memcpy(cmd + sizeof(CMD)-1, sr->stp_filename, sizeof(stp_template));
	sr->stap_popen = popen(cmd, "re");

	return sr->stap_popen ? 0 : -errno;
#undef CMD
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
	struct stap_reader *sr = (struct stap_reader*) pr;
	if (sr->stap_popen)
		pclose(sr->stap_popen);
	unlink(sr->stp_filename);
	free(sr->line);
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
