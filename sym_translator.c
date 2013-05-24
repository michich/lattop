/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "rbtree.h"
#include "sym_translator.h"

struct symbol {
	struct rb_node rb_node;
	unsigned long addr;
	char *name;
};

static struct rb_root addr2fun = RB_ROOT;

static struct symbol *__insert_symbol(unsigned long addr, struct symbol *s)
{
	struct rb_node **p = &addr2fun.rb_node;
	struct rb_node *parent = NULL;
	struct symbol *symbol;

	while (*p) {
		parent = *p;
		symbol = rb_entry(parent, struct symbol, rb_node);

		if (addr < symbol->addr)
			p = &(*p)->rb_left;
		else if (addr > symbol->addr)
			p = &(*p)->rb_right;
		else
			return symbol;
	}

	rb_link_node(&s->rb_node, parent, p);

	return NULL;
}

static struct symbol *insert_symbol(unsigned long addr, struct symbol *s)
{
	struct symbol *ret;
	if ((ret = __insert_symbol(addr, s))) {
		/* we already know this address. some alias? */
		free(s->name);
		free(s);
		goto out;
	}
	rb_insert_color(&s->rb_node, &addr2fun);
out:
	return ret;
}

static int parse_kallsyms(void)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	unsigned long addr;
	char type;
	char *name;
	struct symbol *s;

	f = fopen("/proc/kallsyms", "re");
	if (!f) {
		perror("/proc/kallsyms");
		return -errno;
	}

	while ((read = getline(&line, &len, f)) != -1) {
		sscanf(line, "%lx %c %ms", &addr, &type, &name);

		/* only interested in code */
		if (type != 't' && type != 'T') {
			free(name);
			continue;
		}

		s = malloc(sizeof(struct symbol));
		s->addr = addr;
		s->name = name;
		insert_symbol(addr, s);
	}
	if (line)
		free(line);
	fclose(f);
	return 0;
}

struct symbol *lookup_lower_bound(unsigned long ip)
{
	struct rb_node *n = addr2fun.rb_node;
	struct symbol *symbol, *lower_bound = NULL;

	while (n) {
		symbol = rb_entry(n, struct symbol, rb_node);

		if (ip < symbol->addr)
			n = n->rb_left;
		else if (ip > symbol->addr) {
			n = n->rb_right;
			lower_bound = symbol;
		}
		else
			return symbol;
	}
	return lower_bound;
}

static void delete_symbols(struct rb_node *n)
{
	struct symbol *s;
	if (!n)
		return;
	delete_symbols(n->rb_left);
	delete_symbols(n->rb_right);
	s = rb_entry(n, struct symbol, rb_node);
	free(s->name);
	free(s);
}

const char *sym_translator_lookup(unsigned long ip)
{
	struct symbol *s = lookup_lower_bound(ip);
	if (s)
		return s->name;
	else
		return NULL;
}

/*void sym_translator_dump(void)
{
	struct rb_node *node;
	struct symbol *s;
	for (node = rb_first(&addr2fun); node; node = rb_next(node)) {
		s = rb_entry(node, struct symbol, rb_node);
		printf("%lx => %s\n", s->addr, s->name);
	}
}*/

int sym_translator_init(void)
{
	return parse_kallsyms();
}

void sym_translator_fini(void)
{
	delete_symbols(addr2fun.rb_node);
}
