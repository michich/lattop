/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _POLLED_READER_H
#define _POLLED_READER_H

struct polled_reader;

struct polled_reader_ops {
	void (*fini)(struct polled_reader *pr);
	int (*start)(struct polled_reader *pr);
	int (*get_fd)(struct polled_reader *pr);
	int (*handle_ready_fd)(struct polled_reader *pr);
};

struct polled_reader {
	const struct polled_reader_ops *ops;
};

#endif
