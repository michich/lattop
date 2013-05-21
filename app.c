/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <poll.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "sym_translator.h"

#define NUM_READERS 2

static struct polled_reader *readers[NUM_READERS];
static struct process_accountant accountant;
static int should_quit;

int app_run(void)
{
	struct pollfd fds[NUM_READERS];
	int nready, i;
	int err = 0;

	memset(fds, 0, sizeof(fds));
	for (i = 0; i < NUM_READERS; i++) {
		fds[i].fd = readers[i]->get_fd(readers[i]);
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
				err = readers[i]->handle_ready_fd(readers[i]);
				if (err) {
					should_quit = 1;
					break;
				}
			}
		}
	}

	return err;
}

void app_dump(void)
{
	pa_dump(&accountant);
	pa_clear_all(&accountant);
}

void app_quit(void)
{
	should_quit = 1;
}

struct process_accountant *app_getPA(void)
{
	return &accountant;
}

int app_init(void)
{
	int err, i;
	struct sched_param schedp;

	err = sym_translator_init();
	if (err) {
		fprintf(stderr, "Failed to init the symbol map.\n");
		return 1;
	}

	pa_init(&accountant);

	readers[0] = stap_reader_new();
	readers[1] = command_reader_new();

	for (i = 0; i < NUM_READERS; i++) {
		err = readers[i]->start(readers[i]);
		if (err) {
			fprintf(stderr, "Failed to start a reader.\n");
			return 1;
		}
	}

	memset(&schedp, 0, sizeof(schedp));
	schedp.sched_priority = 10;
	sched_setscheduler(0, SCHED_FIFO, &schedp);

	return 0;
}

void app_fini(void)
{
	int i;
	for (i = 0; i < NUM_READERS; i++) {
		if (readers[i]->fini)
			readers[i]->fini(readers[i]);
		free(readers[i]);
	}
	pa_fini(&accountant);
	sym_translator_fini();
}


