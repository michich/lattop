/*
 * stap_reader reads stack traces of waking processes using a SystemTap script
 *
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stap_reader.h"

#include "back_trace.h"
#include "process_accountant.h"
#include "lattop.h"

struct stap_reader {
	/* must be first */
	struct polled_reader pr;

	int pipe[2];
	pid_t stap_pid;

	enum { STAP_STARTING, STAP_WANT_PROC_INFO, STAP_WANT_LATENCY } state;

	/* currently processed line of input */
	char *line;
	size_t len;

	/* ring buffer for reading input pipe */
	char buf[64*1024];
	unsigned start, fill_count;

	/* data read in state STAP_WANT_PROC_INFO */
	char comm[16]; /* TASK_COMM_LEN */
	unsigned long delay;
	unsigned long pid;
	unsigned long tid;
	char sleep_or_block;
};

static int stap_reader_start(struct polled_reader *pr)
{
	struct stap_reader *sr = (struct stap_reader*) pr;
	pid_t pid;
	int r;

	r = pipe2(sr->pipe, 0);
	if (r < 0) {
		r = -errno;
		perror("pipe");
		goto out_pipe;
	}

	pid = fork();
	if (pid < 0) {
		r = -errno;
		perror("fork");
		goto out_fork;
	}

	if (pid == 0) {
		/* child */
		char *argv[7];
		unsigned n;

		close(sr->pipe[0]);
		r = dup2(sr->pipe[1], STDOUT_FILENO);
		if (r < 0) {
			perror("dup2");
			exit(1);
		}

		n = 0;
		argv[n++] = "stap";
		argv[n++] = "-g";
		argv[n++] = "lat.stp";
		asprintf(&argv[n++], "%llu", arg_min_delay);
		asprintf(&argv[n++], "%llu", arg_max_interruptible_delay);
		asprintf(&argv[n++], "%llu", arg_pid_filter);
		argv[n++] = NULL;

		execvp("stap", argv);

		/* only on failure */
		perror("Executing stap");
		exit(1);
	}

	/* parent */
	sr->stap_pid = pid;
	close(sr->pipe[1]);
	fcntl(sr->pipe[0], F_SETFL,
	      fcntl(sr->pipe[0], F_GETFL, 0) | O_NONBLOCK);

	return 0;

out_fork:
	close(sr->pipe[0]);
	close(sr->pipe[1]);
out_pipe:
	return r;
}

static ssize_t buf_refill(struct stap_reader *sr)
{
	struct iovec iovecs[2];
	int iovcnt;
	ssize_t nread;

	unsigned space = sizeof(sr->buf) - sr->fill_count;
	if (!space)
		return -ENOSPC;

	unsigned first_empty_idx = (sr->start + sr->fill_count) % sizeof(sr->buf);
	char *first_empty = sr->buf + first_empty_idx;

	if (space <= sizeof(sr->buf) - first_empty_idx) {
		/* no wrap */
		iovecs[0].iov_base = first_empty;
		iovecs[0].iov_len = space;
		iovcnt = 1;
	} else {
		/* wrap */
		iovecs[0].iov_base = first_empty;
		iovecs[0].iov_len = sizeof(sr->buf) - first_empty_idx;
		iovecs[1].iov_base = sr->buf;
		iovecs[1].iov_len = space - iovecs[0].iov_len;
		iovcnt = 2;
	}

	nread = readv(sr->pipe[0], iovecs, iovcnt);
	if (nread < 0)
		return -errno;

	sr->fill_count += nread;

	return nread;
}

static char *bufchr(struct stap_reader *sr, char c)
{
	char *p;
	unsigned first_part_len = sizeof(sr->buf) - sr->start;

	if (sr->fill_count <= first_part_len)
		/* no wrap */
		return memchr(sr->buf + sr->start, c, sr->fill_count);
	else {
		/* wrap */
		p = memchr(sr->buf + sr->start, c, first_part_len);
		if (p)
			return p;
		return memchr(sr->buf, c, sr->fill_count - first_part_len);
	}
}

static ssize_t get_next_line(struct stap_reader *sr)
{
	char *start = sr->buf + sr->start;
	char *eol, *p;
	unsigned line_len; /* including the \n, NOT including \0 */

	eol = bufchr(sr, '\n');
	if (!eol)
		return -ENOENT;

	line_len = eol - start + 1;
	if (eol < start)  /* wrap */
		line_len += sizeof(sr->buf);

	if (sr->len <= line_len) {
		char *reallocd_line;
		reallocd_line = realloc(sr->line, line_len + 1);
		if (!reallocd_line)
			return -ENOMEM;
		sr->line = reallocd_line;
		sr->len  = line_len + 1;
	}

	if (eol >= start) {
		/* no wrap */
		p = mempcpy(sr->line, start, line_len);
		*p = '\0';
	} else {
		/* wrap */
		unsigned first_part_len = sizeof(sr->buf) - sr->start;
		p = mempcpy(sr->line, start, first_part_len);
		p = mempcpy(p, sr->buf, line_len - first_part_len);
		*p = '\0';
	}

	/* consume the line */
	sr->start = eol - sr->buf + 1;
	if (sr->start == sizeof(sr->buf))
		sr->start = 0;
	sr->fill_count -= line_len;

	return line_len;
}

static int read_all_lines(struct stap_reader *sr)
{
	ssize_t n;
	char *str;
	struct back_trace bt;
	int depth;
	int nread;
	bool end_of_trace;

	for (;;) {
		n = get_next_line(sr);
		if (n < 0) {
			if (n == -ENOENT)
				/* no complete line, must read more */
				return 0;
			return n;
		}

		switch (sr->state) {
		case STAP_STARTING:
			if (strcmp(sr->line, "lat begin\n"))
				return -EINVAL;

			lattop_reader_started(&sr->pr);
			sr->state = STAP_WANT_PROC_INFO;
			break;

		case STAP_WANT_PROC_INFO:
			if (sscanf(sr->line, "%c %lu %lu %lu %15[^\n]", &sr->sleep_or_block, &sr->delay, &sr->pid, &sr->tid, sr->comm) != 5) {
				fprintf(stderr, "Malformed input line.\n");
				return -EINVAL;
			}
			sr->state = STAP_WANT_LATENCY;
			break;

		case STAP_WANT_LATENCY:
			str = sr->line;
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

			pa_account_latency(sr->pid, sr->tid, sr->comm, sr->delay, &bt);
			sr->state = STAP_WANT_PROC_INFO;
			break;
		}
	}
}

static int stap_reader_handle_ready_fd(struct polled_reader *pr)
{
	struct stap_reader *sr = (struct stap_reader*) pr;
	ssize_t refill_result;
	int r, i;

	/* Finite loop count in order to give other polled readers a chance */
	for (i = 0; i < 100; i++) {
		refill_result = buf_refill(sr);
		switch (refill_result) {
		case 0:
			return -1;
		case -EAGAIN:
			return 0;
		case -ENOSPC:
			fprintf(stderr, "No space in read buffer before refilling. Weird.\n");
			/* try to recover by dropping it all */
			sr->fill_count = 0;
			return 0;
		default:;
		}

		r = read_all_lines(sr);
		if (r < 0)
			return r;
	}

	return 0;
}

static void stap_reader_fini(struct polled_reader *pr)
{
	struct stap_reader *sr = (struct stap_reader*) pr;
	if (sr->stap_pid) {
		int r, status;

		close(sr->pipe[0]);
		kill(sr->stap_pid, SIGTERM);
		kill(sr->stap_pid, SIGCONT);

		do {
			r = waitpid(sr->stap_pid, &status, 0);
			if (r < 0 && errno != EINTR) {
				perror("waitpid");
				break;
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	}
	free(sr->line);
}

static int stap_reader_get_fd(struct polled_reader *pr)
{
	struct stap_reader *sr = (struct stap_reader*) pr;
	return sr->pipe[0];
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
