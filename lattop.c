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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "lattop.h"

#include "process_accountant.h"
#include "sym_translator.h"
#include "lat_translator.h"

#include "polled_reader.h"
#include "timer_reader.h"
#include "stap_reader.h"

#define MAX_READERS 2

int arg_interval = 5;
int arg_count;
enum sort_by arg_sort;
bool arg_reverse;

static struct polled_reader *readers[MAX_READERS];
static struct pollfd poll_fds[MAX_READERS];
static unsigned num_readers;

static int should_quit;

static int start_reader(unsigned index)
{
	int r;

	if (!readers[index]) {
		fprintf(stderr, "Out of memory.\n");
		return -ENOMEM;
	}

	if (readers[index]->ops->start) {
		r = readers[index]->ops->start(readers[index]);
		if (r) {
			fprintf(stderr, "Failed to start reader #%d: %s\n",
				index, strerror(-r));
			return r;
		}
	}

	poll_fds[index].fd = readers[index]->ops->get_fd(readers[index]);
	poll_fds[index].events = POLLIN;
	return 0;
}

void lattop_reader_started(struct polled_reader *r)
{
	/* stap reader */
	assert(readers[0] == r);
	assert(num_readers == 1);
	assert(num_readers < MAX_READERS);

	readers[num_readers] = timer_reader_new();
	start_reader(num_readers);
	num_readers++;
}

static int main_loop(void)
{
	int nready, i;
	int r = 0;

	while (!should_quit) {
		nready = poll(poll_fds, num_readers, -1);
		if (nready < 0) {
			perror("poll");
			return 1;
		}

#if 0
		/* always process latest events in perf_reader */
		fds[0].revents |= POLLIN;
#endif
	
		for (i = 0; i < num_readers; i++) {
			if (!poll_fds[i].revents)
				continue;

			r = readers[i]->ops->handle_ready_fd(readers[i]);
			if (r) {
				should_quit = 1;
				if (r > 0)
					r = 0;
				break;
			}
		}
	}

	return r;
}

static void fini(void)
{
	int i;
	for (i = 0; i < num_readers; i++) {
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

	assert(num_readers < MAX_READERS);
	readers[num_readers++] = stap_reader_new();

	printf("Initializing...\n");

	for (i = 0; i < num_readers; i++) {
		r = start_reader(i);
		if (r < 0)
			goto err;
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
	fprintf(stderr,
"Usage: lattop [-i INTERVAL] [-c COUNT] [-s SORT_BY] [-r]\n"
"  -i, --interval=INTERVAL      time in seconds between measurements (default: 5)\n"
"  -c, --count=COUNT            stop after COUNT measurements\n"
"  -s, --sort=SORT_BY           sort the output by one of:\n"
"                                'max'      maximum latency (default)\n"
"                                'total'    total latency\n"
"                                'pid'      pid of the process\n"
"  -r, --reverse                reverse the sort order\n");
	exit(code);
}

static void parse_argv(int argc, char *argv[])
{
	int c, i, option_index = 0;

	static const struct option long_options[] = {
		{ "interval", required_argument, 0, 'i' },
		{ "count",    required_argument, 0, 'c' },
		{ "reverse",  no_argument,       0, 'r' },
		{ "sort",     required_argument, 0, 's' },
		{ "help",     no_argument,       0, 'h' },
		{ 0,          0,                 0,  0  }
	};

	static const char *sort_types[_NR_SORT_BY] = {
		[SORT_BY_MAX_LATENCY]   = "max",
		[SORT_BY_TOTAL_LATENCY] = "total",
		[SORT_BY_PID]           = "pid",
	};

	for (;;) {
		c = getopt_long(argc, argv, "i:c:rs:h", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			arg_interval = atoi(optarg);
			if (arg_interval <= 0) {
				fprintf(stderr, "Interval must be a positive number.\n");
				exit(1);
			}
			break;
		case 'c':
			arg_count = atoi(optarg);
			if (arg_interval < 0) {
				fprintf(stderr, "Count must be positive (or zero for infinite).\n");
				exit(1);
			}
			break;
		case 'r':
			arg_reverse = true;
			break;
		case 's':
			for (i = 0; i < _NR_SORT_BY; i++) {
				if (!strcasecmp(optarg, sort_types[i]))
					break;
			}

			if (i == _NR_SORT_BY) {
				fprintf(stderr, "Unknown sort type '%s'.\n", optarg);
				exit(1);
			}

			arg_sort = i;

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
