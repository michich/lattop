/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <limits.h>
#include <stdio.h>

#include "back_trace.h"
#include "sym_translator.h"
#include "lat_translator.h"

int bt_compare(const struct back_trace *b1, const struct back_trace *b2)
{
	int i;
	for (i = 0; i < MAX_BT_LEN; i++) {
		if (b1->trace[i] < b2->trace[i])
			return -1;
		if (b1->trace[i] > b2->trace[i])
			return +1;
	}
	return 0;
}

void bt_dump(const struct back_trace *b)
{
	const char *translation, *best_translation = NULL;
	int prio, best_prio = 0;
	int i;
	for (i = 0; i < MAX_BT_LEN; i++) {
		if (b->trace[i] == 0 || b->trace[i] == ULONG_MAX)
			break;
		const char *fun = sym_translator_lookup(b->trace[i]);
		if (fun == NULL) {
			fprintf(stderr, "Could not translate %lx\n", b->trace[i]);
			break;
		}
		if (i != 0)
			printf(" ");
		printf("%s", fun);

		if (lat_translator_lookup(fun, &translation, &prio) == 0) {
			if (prio > best_prio) {
				best_prio = prio;
				best_translation = translation;
			}
		}
	}

	if (best_translation)
		printf(" [%s]", best_translation);
}

void bt_init(struct back_trace *b, const long tr[], int len)
{
	int i;
	for (i = 0; i < len && i < MAX_BT_LEN; i++)
		b->trace[i] = tr[i];
}
