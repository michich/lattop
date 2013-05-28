#ifndef _LATTOP_H
#define _LATTOP_H

#include <stdbool.h>

#include "polled_reader.h"

void lattop_reader_started(struct polled_reader *r);

enum sort_by {
	SORT_BY_MAX_LATENCY,
	SORT_BY_TOTAL_LATENCY,
	SORT_BY_PID,
	_NR_SORT_BY
};

extern int arg_interval;
extern int arg_count;
extern enum sort_by arg_sort;
extern bool arg_reverse;
extern unsigned long arg_min_delay_us;
extern unsigned long arg_max_interruptible_delay_us;

#endif
