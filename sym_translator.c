/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <sys/types.h>
#include <assert.h>
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

struct symbol_slab {
	struct symbol_slab *next;
	unsigned long fill_count;
	struct symbol symbols[];
};
#define SLAB_SIZE (128*1024)
#define SYMBOLS_PER_SLAB ((SLAB_SIZE - sizeof(struct symbol_slab)) / sizeof(struct symbol))

static struct symbol_slab *first_slab, *current_slab;

/* tree of symbols, only used during kallsyms parsing */
static struct rb_root addr2fun = RB_ROOT;
/* after parsing, two simple arrays suffice */
static unsigned long *addr_array;  /* sorted for binary search */
static char **name_array;
static unsigned n_symbols;

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
		/* we're always inserting the last allocated symbol,
		 * so freeing it from slab is trivial: */
		current_slab->fill_count--;
		goto out;
	}
	rb_insert_color(&s->rb_node, &addr2fun);
	n_symbols++;
out:
	return ret;
}

static void delete_names_from_tree(struct rb_node *n)
{
	struct symbol *s;

	/* only delete names here if the array hasn't taken ownership of them yet */
	if (!n || name_array)
		return;
	delete_names_from_tree(n->rb_left);
	delete_names_from_tree(n->rb_right);
	s = rb_entry(n, struct symbol, rb_node);

	free(s->name);
}

static void delete_slabs(void)
{
	struct symbol_slab *ss, *next;

	for (ss = first_slab; ss; ss = next) {
		next = ss->next;
		free(ss);
	}

	current_slab = first_slab = NULL;
}

static struct symbol_slab *new_slab(void)
{
	struct symbol_slab *ss;

	ss = malloc(SLAB_SIZE);
	if (!ss)
		return NULL;
	ss->fill_count = 0;
	ss->next = NULL;
	return ss;
}

static struct symbol *new_symbol(unsigned long addr, char *name)
{
	struct symbol_slab *ss;
	struct symbol *s;

	if (!first_slab || current_slab->fill_count == SYMBOLS_PER_SLAB) {
		ss = new_slab();
		if (!ss)
			return NULL;

		if (!first_slab)
			current_slab = first_slab = ss;
		else {
			current_slab->next = ss;
			current_slab = ss;
		}
	}

	s = &current_slab->symbols[current_slab->fill_count++];
	s->addr = addr;
	s->name = name;
	return s;
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
	int r = 0;

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

		s = new_symbol(addr, name);
		if (!s) {
			perror("Allocating memory for symbols");
			delete_names_from_tree(addr2fun.rb_node);
			delete_slabs();
			addr2fun = RB_ROOT;
			r = -ENOMEM;
			goto out;
		}
		insert_symbol(addr, s);
	}

out:
	free(line);
	fclose(f);
	return r;
}

static int build_arrays(void)
{
	struct rb_node *node;
	unsigned i;

	addr_array = malloc(n_symbols * sizeof(unsigned long));
	if (!addr_array)
		return -ENOMEM;

	name_array = malloc(n_symbols * sizeof(char*));
	if (!name_array) {
		free(addr_array);
		addr_array = NULL;
		return -ENOMEM;
	}

	i = 0;
	for (node = rb_first(&addr2fun); node; node = rb_next(node)) {
		struct symbol *symbol = rb_entry(node, struct symbol, rb_node);

		addr_array[i] = symbol->addr;
		name_array[i] = symbol->name; /* the array takes ownership of name */
		i++;
	}
	assert(i == n_symbols);

	/* the tree is not needed anymore */
	delete_names_from_tree(addr2fun.rb_node);
	delete_slabs();
	addr2fun = RB_ROOT;

	return 0;
}

void delete_arrays(void)
{
	unsigned i;

	for (i = 0; i< n_symbols; i++)
		free(name_array[i]);
	free(name_array);
	name_array = NULL;

	free(addr_array);
	addr_array = NULL;
}

const char *sym_translator_lookup(unsigned long ip)
{
	unsigned low, high, middle;
	if (!addr_array || !name_array || n_symbols == 0)
		return NULL;

	low = 0;
	high = n_symbols - 1;

	if (ip < addr_array[low])
		return NULL;

	if (ip >= addr_array[high])
		return name_array[high];

	/* Invariant: addr_array[low] <= ip < addr_array[high] */

	while (high - low > 1) {
		middle = (low + high) / 2;
		if (addr_array[middle] <= ip)
			low = middle;
		else
			high = middle;
	}

	return name_array[low];
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
	int r;

	r = parse_kallsyms();
	if (r)
		return r;

	return build_arrays();
}

void sym_translator_fini(void)
{
	delete_names_from_tree(addr2fun.rb_node);
	delete_slabs();
	delete_arrays();
}
