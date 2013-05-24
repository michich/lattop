/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#ifndef _APP_H
#define _APP_H

#include "process_accountant.h"
#include "perf_reader.h"
#include "command_reader.h"
#include "stap_reader.h"

void app_dump(void);
void app_quit(void);
struct process_accountant *app_getPA(void);

#endif
