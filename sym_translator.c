/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <sys/mman.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rbtree.h"
#include "sym_translator.h"

struct symbol {
	struct rb_node rb_node;
	unsigned long addr;
	ptrdiff_t name_offset;  /* relative to all_names */
};

struct symbol_slab {
	struct symbol_slab *next;
	unsigned long fill_count;
	struct symbol symbols[];
};
#define SLAB_SIZE (1024*1024)
#define SYMBOLS_PER_SLAB ((SLAB_SIZE - sizeof(struct symbol_slab)) / sizeof(struct symbol))

#define NAMES_INCREMENT (128*1024)

static struct symbol_slab *first_slab, *current_slab;

/* tree of symbols, only used during kallsyms parsing */
static struct rb_root addr2fun = RB_ROOT;

static unsigned long *addr_array;  /* sorted for binary search */
static char **name_array; /* pointers into all_names */
static char *all_names;   /* storage for all names: "name\0second_name\0third_name\0..." */
static size_t all_names_alloc, all_names_end;
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

static void delete_slabs(void)
{
	struct symbol_slab *ss, *next;

	for (ss = first_slab; ss; ss = next) {
		next = ss->next;
		munmap(ss, SLAB_SIZE);
	}

	current_slab = first_slab = NULL;
}

static struct symbol_slab *new_slab(void)
{
	struct symbol_slab *ss;

	ss = mmap(NULL, SLAB_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (ss == MAP_FAILED)
		return NULL;
	ss->fill_count = 0;
	ss->next = NULL;
	return ss;
}

static struct symbol *new_symbol(unsigned long addr, ptrdiff_t name_offset)
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
	s->name_offset = name_offset;
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
		r = -errno;
		perror("/proc/kallsyms");
		goto err;
	}

	all_names_alloc = NAMES_INCREMENT;
	all_names = mmap(NULL, all_names_alloc, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (all_names == MAP_FAILED) {
		perror("Allocating memory for symbol names");
		all_names = NULL;
		r = -ENOMEM;
		goto err;
	}

	while ((read = getline(&line, &len, f)) != -1) {
		if (all_names_alloc - all_names_end < 1024) {
			char *new_names;
			new_names = mremap(all_names, all_names_alloc, all_names_alloc + NAMES_INCREMENT, MREMAP_MAYMOVE);
			if (new_names == MAP_FAILED) {
				perror("Allocating memory for symbol names");
				r = -ENOMEM;
				goto err;
			}
			all_names = new_names;
			all_names_alloc += NAMES_INCREMENT;
		}

		name = all_names + all_names_end;

		if (sscanf(line, "%lx %c %1023s", &addr, &type, name) != 3) {
			fprintf(stderr, "Failed to parse line: %s", line);
			continue;
		}

		/* only interested in code */
		if (type != 't' && type != 'T')
			continue;

		s = new_symbol(addr, name - all_names);
		if (!s) {
			perror("Allocating memory for symbols");
			r = -ENOMEM;
			goto err;
		}

		if (!insert_symbol(addr, s))
			all_names_end += strlen(name) + 1;
	}

	if (all_names_end == 0)
		goto err;

	/* trim all_names to minimal required size */
	all_names = mremap(all_names, all_names_alloc, all_names_end, 0);
	assert(all_names != MAP_FAILED);
	all_names_alloc = all_names_end;

	mprotect(all_names, all_names_alloc, PROT_READ);

	free(line);
	fclose(f);
	return 0;

err:
	delete_slabs();
	addr2fun = RB_ROOT;

	if (all_names) {
		munmap(all_names, all_names_alloc);
		all_names = NULL;
	}

	free(line);

	if (f)
		fclose(f);
	return r;
}

static void delete_arrays(void)
{
	if (all_names) {
		munmap(all_names, all_names_alloc);
		all_names = NULL;
	}

	if (name_array) {
		munmap(name_array, n_symbols * sizeof(char*));
		name_array = NULL;
	}

	if (addr_array) {
		munmap(addr_array, n_symbols * sizeof(unsigned long));
		addr_array = NULL;
	}
}

static int build_arrays(void)
{
	struct rb_node *node;
	unsigned i;

	addr_array = mmap(NULL, n_symbols * sizeof(unsigned long), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (addr_array == MAP_FAILED) {
		addr_array = NULL;
		goto oom;
	}

	name_array = mmap(NULL, n_symbols * sizeof(char*), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (name_array == MAP_FAILED) {
		name_array = NULL;
		goto oom;
	}

	i = 0;
	for (node = rb_first(&addr2fun); node; node = rb_next(node)) {
		struct symbol *symbol = rb_entry(node, struct symbol, rb_node);

		addr_array[i] = symbol->addr;
		name_array[i] = all_names + symbol->name_offset;

		i++;
	}
	assert(i == n_symbols);

	/* the tree is not needed anymore */
	delete_slabs();
	addr2fun = RB_ROOT;

	mprotect(addr_array, n_symbols * sizeof(unsigned long), PROT_READ);
	mprotect(name_array, n_symbols * sizeof(char*), PROT_READ);

	return 0;
oom:
	delete_arrays();
	return -ENOMEM;
}

const char *sym_translator_lookup(unsigned long ip)
{
	unsigned low, high, middle;
	if (!addr_array || !name_array || !all_names || n_symbols == 0)
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
	delete_slabs();
	delete_arrays();
}
