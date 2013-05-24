/*
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _LAT_TRANSLATOR_H
#define _LAT_TRANSLATOR_H

int  lat_translator_init(void);
void lat_translator_fini(void);
void lat_translator_dump(void);
int lat_translator_lookup(const char *symbol, const char **translation, int *prio);

#endif
