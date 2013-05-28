#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "timespan.h"

char *format_ms(char *buf, size_t l, uint64_t usec)
{
	/* XXX check l */
	int prec;
	if (usec >= 100*1000)
		prec = 0;
	else if (usec >= 10*1000)
		prec = 1;
	else if (usec >=  1*1000)
		prec = 2;
	else
		prec = 3;
	sprintf(buf, "%*.*f%*s", 4+(prec?:-1), prec, usec / 1000.0, 5-(prec?:-1), "ms");
	return buf;
}


