/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _BACK_TRACE_H
#define _BACK_TRACE_H

#define MAX_BT_LEN 12

struct back_trace {
	unsigned long trace[MAX_BT_LEN];
};

void bt_init(struct back_trace *b, const long tr[], int len);
int  bt_compare(const struct back_trace *b1, const struct back_trace *b2);
void bt_save_symbolic(const struct back_trace *b, char *buf, size_t buflen);

#endif
