#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "timespan.h"

/* format_timespan() originally copied from systemd,
 *     Copyright 2010 Lennart Poettering */

#undef MIN
#define MIN(a,b)                                \
        __extension__ ({                        \
                        typeof(a) _a = (a);     \
                        typeof(b) _b = (b);     \
                        _a < _b ? _a : _b;      \
                })

#define ELEMENTSOF(x) (sizeof(x)/sizeof((x)[0]))

char *format_ms(char *buf, size_t l, uint64_t usec, unsigned significant_digits)
{
	uint64_t tmp;
	unsigned log;
	int prec;

	log = 0;
	tmp = usec / 1000;
	while (tmp > 0) {
		tmp /= 10;
		log++;
	}

	prec = significant_digits - MIN(log, significant_digits);

	snprintf(buf, l, "%*.*f%*s", 4+(prec?:-1), prec, usec / 1000.0, 5-(prec?:-1), "ms");
	return buf;
}
#if 0
char *format_timespan(char *buf, size_t l, uint64_t t, uint64_t accuracy) {
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

        unsigned i;
        char *p = buf;
        bool something = false;

        assert(buf);
        assert(l > 0);

        if (t == (uint64_t) -1)
                return NULL;

        if (t <= 0) {
                snprintf(p, l, "0");
                p[l-1] = 0;
                return p;
        }

        /* The result of this function can be parsed with parse_sec */

        for (i = 0; i < ELEMENTSOF(table); i++) {
                int k;
                size_t n;
                bool done = false;
                uint64_t a, b;

                if (t <= 0)
                        break;

                if (t < accuracy && something)
                        break;

                if (t < table[i].usec)
                        continue;

                if (l <= 1)
                        break;

                a = t / table[i].usec;
                b = t % table[i].usec;

                /* Let's see if we should shows this in dot notation */
                if (t < USEC_PER_MINUTE && b > 0) {
                        uint64_t cc;
                        int j;

                        j = 0;
                        for (cc = table[i].usec; cc > 1; cc /= 10)
                                j++;

                        for (cc = accuracy; cc > 1; cc /= 10) {
                                b /= 10;
                                j--;
                        }

                        if (j > 0) {
                                k = snprintf(p, l,
                                             "%s%llu.%0*llu%s",
                                             p > buf ? " " : "",
                                             (unsigned long long) a,
                                             j,
                                             (unsigned long long) b,
                                             table[i].suffix);

                                t = 0;
                                done = true;
                        }
                }

                /* No? Then let's show it normally */
                if (!done) {
                        k = snprintf(p, l,
                                     "%s%llu%s",
                                     p > buf ? " " : "",
                                     (unsigned long long) a,
                                     table[i].suffix);

                        t = b;
                }

                n = MIN((size_t) k, l);

                l -= n;
                p += n;

                something = true;
        }

        *p = 0;

        return buf;
}
#endif
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
