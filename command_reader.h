/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _COMMAND_READER_H
#define _COMMAND_READER_H

#include "polled_reader.h"

#define READER_BUFLEN 100

struct command_reader {
	/* must be first */
	struct polled_reader pr;

	char buffer[READER_BUFLEN];
};

struct polled_reader *command_reader_new(void);

#endif
