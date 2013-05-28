/*
 * Copyright 2013 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */

#ifndef _TIMER_READER_H
#define _TIMER_READER_H

#include "polled_reader.h"

struct polled_reader *timer_reader_new(int interval, int count);

#endif
