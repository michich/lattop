/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "process_accountant.h"
#include "sym_translator.h"
#include "lat_translator.h"

#include "polled_reader.h"
#include "command_reader.h"
#include "stap_reader.h"

#define NUM_READERS 2

static struct polled_reader *readers[NUM_READERS];
static int should_quit;

static int arg_interval = 5;
static int arg_count;

static int main_loop(void)
{
	struct pollfd fds[NUM_READERS];
	int nready, i;
	int r = 0;

	memset(fds, 0, sizeof(fds));
	for (i = 0; i < NUM_READERS; i++) {
		fds[i].fd = readers[i]->ops->get_fd(readers[i]);
		fds[i].events = POLLIN;
	}

	while (!should_quit) {
		nready = poll(fds, NUM_READERS, -1);
		if (nready < 0) {
			perror("poll");
			return 1;
		}

#if 0
		/* always process latest events in perf_reader */
		fds[0].revents |= POLLIN;
#endif
	
		for (i = 0; i < NUM_READERS; i++) {
			if (fds[i].revents) {
				r = readers[i]->ops->handle_ready_fd(readers[i]);
				if (r) {
					should_quit = 1;
					if (r > 0)
						r = 0;
					break;
				}
			}
		}
	}

	return r;
}

static void fini(void)
{
	int i;
	for (i = 0; i < NUM_READERS; i++) {
		if (readers[i]->ops->fini)
			readers[i]->ops->fini(readers[i]);
		free(readers[i]);
	}
	pa_fini();
	lat_translator_fini();
	sym_translator_fini();
}

static int init(void)
{
	int r, i;
	struct sched_param schedp;

	r = sym_translator_init();
	if (r) {
		fprintf(stderr, "Failed to init the symbol map.\n");
		goto err;
	}

	r = lat_translator_init();
	if (r)
		fprintf(stderr, "Warning: Failed to load latencytop translations.\n");

	pa_init();

	i = 0;
	readers[i++] = stap_reader_new();
	readers[i++] = command_reader_new();
	assert(i == NUM_READERS);

	printf("Starting...\n");

	for (i = 0; i < NUM_READERS; i++) {
		if (!readers[i]) {
			fprintf(stderr, "Out of memory.\n");
			r = -ENOMEM;
			goto err;
		}

		if (!readers[i]->ops->start)
			continue;

		r = readers[i]->ops->start(readers[i]);
		if (r) {
			fprintf(stderr, "Failed to start reader #%d: %s\n",
			                i, strerror(-r));
			goto err;
		}
	}

	memset(&schedp, 0, sizeof(schedp));
	schedp.sched_priority = 10;
	r = sched_setscheduler(0, SCHED_FIFO, &schedp);
	if (r < 0)
		fprintf(stderr, "Warning: Failed to set real-time scheduling policy: %s\n",
		                strerror(errno));

	return 0;
err:
	fini();
	return r;
}

static void usage_and_exit(int code)
{
	fprintf(stderr, "Usage: lattop [-i INTERVAL] [-c COUNT]\n");
	exit(1);
}

static void parse_argv(int argc, char *argv[])
{
	int c, option_index = 0;

	for (;;) {
		static const struct option long_options[] = {
			{ "interval", required_argument, 0, 'i' },
			{ "count",    required_argument, 0, 'c' },
			{ "help",     no_argument,       0, 'h' },
			{ 0,          0,                 0,  0  }
		};

		c = getopt_long(argc, argv, "i:c:h", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			arg_interval = atoi(optarg);
			break;
		case 'c':
			arg_count = atoi(optarg);
			break;
		case 'h':
			usage_and_exit(0);
		case '?':
		default:
			usage_and_exit(1);
		}
	}

	if (optind < argc)
		usage_and_exit(1);
}

int main(int argc, char *argv[])
{
	int ret;

	parse_argv(argc, argv);

	if (init())
		return 1;

	ret = main_loop();

	fini();
	return ret;
}
