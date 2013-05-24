/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "lattop.h"
#include "process_accountant.h"
#include "sym_translator.h"

#include "polled_reader.h"
#include "command_reader.h"
#include "stap_reader.h"

#define NUM_READERS 2

static struct polled_reader *readers[NUM_READERS];
static struct process_accountant accountant;
static int should_quit;

void lattop_dump(void)
{
	pa_dump(&accountant);
	pa_clear_all(&accountant);
}

void lattop_quit(void)
{
	should_quit = 1;
}

struct process_accountant *lattop_getPA(void)
{
	return &accountant;
}


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

		/* always process latest events in perf_reader */
		fds[0].revents |= POLLIN;
	
		for (i = 0; i < NUM_READERS; i++) {
			if (fds[i].revents) {
				r = readers[i]->ops->handle_ready_fd(readers[i]);
				if (r) {
					should_quit = 1;
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
	pa_fini(&accountant);
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

	pa_init(&accountant);

	i = 0;
	readers[i++] = stap_reader_new();
	readers[i++] = command_reader_new();
	assert(i == NUM_READERS);

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

int main()
{
	int ret;

	if (init())
		return 1;

	ret = main_loop();

	fini();
	return ret;
}
