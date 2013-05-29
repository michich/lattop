/*
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#include <sys/signalfd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "signal_reader.h"

struct signal_reader {
	/* must be first */
	struct polled_reader pr;

	int signalfd;
	sigset_t orig_sigmask;
};

static int signal_reader_start(struct polled_reader *pr)
{
	struct signal_reader *sr = (struct signal_reader*) pr;
	sigset_t accept_sigs;
	int r;

	sigemptyset(&accept_sigs);
	sigaddset(&accept_sigs, SIGINT);
	sigaddset(&accept_sigs, SIGTERM);
	sigaddset(&accept_sigs, SIGQUIT);

	r = sigprocmask(SIG_BLOCK, &accept_sigs, &sr->orig_sigmask);
	if (r < 0) {
		r = -errno;
		perror("sigprocmask");
		return r;
	}

	sr->signalfd = signalfd(-1, &accept_sigs, SFD_CLOEXEC);
	if (sr->signalfd < 0) {
		r = -errno;
		perror("signalfd");
		return r;
	}

	return 0;
}

static int signal_reader_handle_ready_fd(struct polled_reader *pr)
{
	struct signal_reader *sr = (struct signal_reader*) pr;
	struct signalfd_siginfo si;
	ssize_t r;

	r = read(sr->signalfd, &si, sizeof(si));
	if (r != sizeof(si)) {
		fprintf(stderr, "Invalid read from signalfd\n");
		return -1;
	}

	switch (si.ssi_signo) {
	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		fprintf(stderr, "Exiting.\n");
		return 1; /* do a clean exit */
	default:
		fprintf(stderr, "Unexpected signal %d received via signalfd.\n", si.ssi_signo);
		return -1;
	}

	return 0;
}

static void signal_reader_fini(struct polled_reader *pr)
{
	struct signal_reader *sr = (struct signal_reader*) pr;
	close(sr->signalfd);
	sigprocmask(SIG_SETMASK, &sr->orig_sigmask, NULL);
}

static int signal_reader_get_fd(struct polled_reader *pr)
{
	struct signal_reader *sr = (struct signal_reader*) pr;
	return sr->signalfd;
}

static const struct polled_reader_ops signal_reader_ops = {
	.fini = signal_reader_fini,
	.start = signal_reader_start,
	.get_fd = signal_reader_get_fd,
	.handle_ready_fd = signal_reader_handle_ready_fd,
};

struct polled_reader *signal_reader_new()
{
	struct signal_reader *r;

	r = calloc(1, sizeof(struct signal_reader));
	if (r == NULL)
		return NULL;

	r->pr.ops = &signal_reader_ops;

	return &r->pr;
}
