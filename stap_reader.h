/*
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#ifndef _STAP_READER_H
#define _STAP_READER_H

#include "polled_reader.h"

struct stap_reader {
	/* must be first */
	struct polled_reader pr;

	FILE *stap_popen;

	char *line;
	size_t len;
};

struct polled_reader *stap_reader_new(void);

#endif
