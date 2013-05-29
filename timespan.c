#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "timespan.h"

/* Originally format_timespan() was copied from systemd,
 *     Copyright 2010 Lennart Poettering
 *     (LGPLv2)
 * but the implementation has been replaced by much simplified and dumber code,
 * which produces different formatting more suited for lattop's needs.
 */

#undef MIN
#define MIN(a,b)                                \
        __extension__ ({                        \
                        typeof(a) _a = (a);     \
                        typeof(b) _b = (b);     \
                        _a < _b ? _a : _b;      \
                })

#define ELEMENTSOF(x) (sizeof(x)/sizeof((x)[0]))

char *format_timespan(char *buf, size_t l, uint64_t usec, unsigned significant_digits) {
        static const struct {
                const char *suffix;
                uint64_t usec;
        } table[] = {
                { "y", USEC_PER_YEAR },
                { "month", USEC_PER_MONTH },
                { "w", USEC_PER_WEEK },
                { "d", USEC_PER_DAY },
                { "h", USEC_PER_HOUR },
                { "min", USEC_PER_MINUTE },
                { "s", USEC_PER_SEC },
                { "ms", USEC_PER_MSEC },
                { "us", 1 },
        };
	uint64_t tmp;
	unsigned log;
	int i, prec, printed;

	assert(l > 0);

	/* "-1" because "us" is the final fallback */
        for (i = 0; i < ELEMENTSOF(table)-1; i++) {
                if (usec >= table[i].usec)
                        break;
	}

	if (i != ELEMENTSOF(table)-1) {
		log = 0;
		tmp = usec / table[i].usec;
		while (tmp > 0) {
			tmp /= 10;
			log++;
		}

		prec = significant_digits - MIN(log, significant_digits);
	} else
		prec = 0;

	printed = snprintf(buf, l, "%.*f %s", prec, (double)usec / table[i].usec, table[i].suffix);
	if (printed >= l) {
		memset(buf, '#', l-1);
		buf[l-1] = '\0';
		return NULL;
	}

        return buf;
}
