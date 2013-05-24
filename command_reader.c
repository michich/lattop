/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "command_reader.h"

#include "process_accountant.h"

static int cr_get_fd(struct polled_reader *pr)
{
	return STDIN_FILENO;
}

static int cr_handle_ready_fd(struct polled_reader *pr)
{
	struct command_reader *cr = (struct command_reader*) pr;
	ssize_t nread;

	nread = read(STDIN_FILENO, cr->buffer, READER_BUFLEN);
	if (nread < 0) {
		perror("stdin read error");
		return 1;
	} else if (nread == 0) {
		/* EOF */
		printf("\n");
		return 1;
	}

	if (!memcmp(cr->buffer, "\n", strlen("\n")) || !memcmp(cr->buffer, "dump\n", strlen("dump\n")))
		pa_dump_and_clear();
	else if (!memcmp(cr->buffer, "version\n", strlen("version\n")))
		printf("0.4\n");
	else if (!memcmp(cr->buffer, "help\n", strlen("help\n")))
		printf("dump, version, help, quit\n");
	else if (!memcmp(cr->buffer, "quit\n", strlen("quit\n")))
		return 1;

	return 0;
}

static const struct polled_reader_ops command_reader_ops = {
	.get_fd = cr_get_fd,
	.handle_ready_fd = cr_handle_ready_fd,
};

struct polled_reader *command_reader_new(void)
{
	struct command_reader *cr;

	cr = calloc(1, sizeof(struct command_reader));
	if (cr == NULL)
		return NULL;

	cr->pr.ops = &command_reader_ops;

	return &cr->pr;
}
