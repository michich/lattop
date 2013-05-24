/*
 * Copyright 2010-2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rbtree.h"
#include "sym_translator.h"

struct symbol_translation {
	struct rb_node rb_node;
	char *symbol;
	char *translation;
	int prio;
};

static struct rb_root sym2trans = RB_ROOT;

static struct symbol_translation *__insert_symbol(const char *symbol, struct symbol_translation *st)
{
	struct rb_node **p = &sym2trans.rb_node;
	struct rb_node *parent = NULL;
	struct symbol_translation *ist;
	int cmp;

	while (*p) {
		parent = *p;
		ist = rb_entry(parent, struct symbol_translation, rb_node);

		cmp = strcmp(symbol, ist->symbol);
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return ist;
	}

	rb_link_node(&st->rb_node, parent, p);

	return NULL;
}

static struct symbol_translation *insert_symbol(const char *symbol, struct symbol_translation *st)
{
	struct symbol_translation *ret;
	if ((ret = __insert_symbol(symbol, st))) {
		/* we already know this address. some alias? */
		free(st->symbol);
		free(st->translation);
		free(st);
		goto out;
	}
	rb_insert_color(&st->rb_node, &sym2trans);
out:
	return ret;
}

static int parse_latencytop_trans(void)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int prio;
	char *symbol;
	char *translation;
	struct symbol_translation *st;

	f = fopen("/usr/share/latencytop/latencytop.trans", "re");
	if (!f) {
		perror("/usr/share/latencytop/latencytop.trans");
		return -errno;
	}

	while ((read = getline(&line, &len, f)) != -1) {
		if (line[0] == '#' || line[0] == '\n')
			continue;

		if (sscanf(line, "%d %ms %m[^\n]", &prio, &symbol, &translation) != 3) {
			fprintf(stderr, "Failed to parse line, ignoring: %s", line);
			continue;
		}

		st = malloc(sizeof(struct symbol_translation));
		st->symbol = symbol;
		st->translation = translation;
		st->prio = prio;
		insert_symbol(symbol, st);
	}
	if (line)
		free(line);
	fclose(f);
	return 0;
}

struct symbol_translation *lookup_symbol(const char *symbol)
{
	struct rb_node *n = sym2trans.rb_node;
	struct symbol_translation *st;
	int cmp;

	while (n) {
		st = rb_entry(n, struct symbol_translation, rb_node);

		cmp = strcmp(symbol, st->symbol);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return st;
	}

	return NULL;
}

static void delete_symbols(struct rb_node *n)
{
	struct symbol_translation *s;
	if (!n)
		return;
	delete_symbols(n->rb_left);
	delete_symbols(n->rb_right);
	s = rb_entry(n, struct symbol_translation, rb_node);
	free(s->symbol);
	free(s->translation);
	free(s);
}

int lat_translator_lookup(const char *symbol, const char **translation, int *prio)
{
	struct symbol_translation *st = lookup_symbol(symbol);

	if (!st)
		return -ENOENT;

	*translation = st->translation;
	*prio = st->prio;
	return 0;
}

void lat_translator_dump(void)
{
	struct rb_node *node;
	struct symbol_translation *st;
	for (node = rb_first(&sym2trans); node; node = rb_next(node)) {
		st = rb_entry(node, struct symbol_translation, rb_node);
		printf("%d %s => %s\n", st->prio, st->symbol, st->translation);
	}
}

int lat_translator_init(void)
{
	return parse_latencytop_trans();
}

void lat_translator_fini(void)
{
	delete_symbols(sym2trans.rb_node);
}
