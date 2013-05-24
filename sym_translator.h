/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _SYM_TRANSLATOR_H
#define _SYM_TRANSLATOR_H

int  sym_translator_init(void);
void sym_translator_fini(void);
/*void sym_translator_dump(void);*/
const char *sym_translator_lookup(unsigned long ip);

#endif
