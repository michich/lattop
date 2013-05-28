/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "back_trace.h"
#include "sym_translator.h"

int bt_compare(const struct back_trace *b1, const struct back_trace *b2)
{
/*	int i;
	for (i = 0; i < MAX_BT_LEN; i++) {
		if (b1->trace[i] < b2->trace[i])
			return -1;
		if (b1->trace[i] > b2->trace[i])
			return +1;
	}
	return 0;*/
	return memcmp(b1->trace, b2->trace, sizeof(b1->trace));
}

void bt_save_symbolic(const struct back_trace *b, char *buf, size_t buflen)
{
	size_t len;
	int i;

	for (i = 0; i < MAX_BT_LEN; i++) {
		if (b->trace[i] == 0 || b->trace[i] == ULONG_MAX)
			break;

		const char *fun = sym_translator_lookup(b->trace[i]);
		if (fun == NULL) {
			fprintf(stderr, "Could not translate %lx\n", b->trace[i]);
			break;
		}

		len = strlen(fun);
		if (len >= buflen - 2)
			break;

		if (i != 0) {
			*buf++ = ' ';
			buflen--;
		}

		memcpy(buf, fun, len);
		buf += len;
		buflen -= len;
	}

	*buf = '\0';
}

void bt_init(struct back_trace *b, const long tr[], int len)
{
	int i;
	for (i = 0; i < len && i < MAX_BT_LEN; i++)
		b->trace[i] = tr[i];
}
