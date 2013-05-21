/*
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * License: GPLv2
 */
#include <stdio.h>
#include "app.h"

int main()
{
	int ret;
	if (app_init()) {
		fprintf(stderr, "app initialization failed\n");
		return 1;
	}

	ret = app_run();

	app_fini();
	return ret;
}
