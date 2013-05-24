/*
 * perf_reader reads sched_latencytop tracepoint events using perf.
 *
 * Copyright 2010 Red Hat Inc.
 * Author: Michal Schmidt
 * Some parts were copied from Linux tools/perf/builtin-record.c written by
 *   Ingo Molnar, Peter Zijlstra and others
 * License: GPLv2
 */

#include <linux/perf_event.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "perf_reader.h"
#include "back_trace.h"
#include "lattop.h"

#include "cpumap.h"
#include "perf.h"

#define DEBUGFS_MNT "/sys/kernel/debug"
#define MMAP_PAGES_SHIFT 7
#define MMAP_PAGES (1 << MMAP_PAGES_SHIFT)

#define min(a,b) ((a) < (b) ? (a) : (b))

static pid_t mypid;
static int   pagesize;
static int trace_type;

static int get_trace_type(void)
{
	FILE *f;
	int ret;

	if (trace_type)
		return trace_type;

	f = fopen(DEBUGFS_MNT "/tracing/events/sched/sched_latencytop/id", "r");
	if (!f)
		return 0;
	ret = fscanf(f, "%d", &trace_type);
	if (ret != 1)
		trace_type = 0;
	fclose(f);
	return trace_type;
}

static void try_set_event_filter(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_SET_FILTER, "inter==0 || delay<=5000000") < 0)
		fprintf(stderr, "Failed to set filter, but this is not fatal\n");
}

static int create_perf_event(struct perf_reader *r, int index)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));

	attr.type		= PERF_TYPE_TRACEPOINT;
	attr.config		= get_trace_type();
	attr.sample_period	= 1;
	attr.sample_type	|= PERF_SAMPLE_RAW;
	attr.disabled		= 1;
	attr.watermark		= 1;
	attr.wakeup_watermark	= (MMAP_PAGES/2) * pagesize;

	if (attr.config <= 0)
		goto err;

	r->fds[index] = sys_perf_event_open(&attr, -1, cpumap[index], -1, 0);
	if (r->fds[index] < 0) {
		perror("Perf syscall failed");
		goto err;
	}
	if (fcntl(r->fds[index], F_SETFL, O_NONBLOCK) < 0) {
		perror("Failed to set O_NONBLOCK");
		goto err;
	}

	try_set_event_filter(r->fds[index]);

	/* multiplex all events into the first one */
	if (index != 0) {
		if (ioctl(r->fds[index], PERF_EVENT_IOC_SET_OUTPUT, r->fds[0]) < 0)
			goto err;
	}

	return 0;
err:
	/* caller will clean up */
	return -1;
}

static int map_perf_buffer(struct perf_reader *r)
{
	r->md.prev = 0;
	r->md.mask = MMAP_PAGES * pagesize - 1;
	r->md.base = mmap(NULL, (MMAP_PAGES+1)*pagesize,
	                 PROT_READ | PROT_WRITE, MAP_SHARED,
	                 r->fds[0], 0);
	if (r->md.base == MAP_FAILED) {
		perror("failed mmap");
		return -1;
	}
	return 0;
}

static void unmap_perf_buffer(struct perf_reader *r)
{
	if (r->md.base != MAP_FAILED) {
		munmap(r->md.base, (MMAP_PAGES+1)*pagesize);
		r->md.base = MAP_FAILED;
	}
}

static int create_perf_events(struct perf_reader *r)
{
	int err, i;
	for (i = 0; i < r->nr_cpus; i++) {
		err = create_perf_event(r, i);
		if (err)
			return err;
	}
	return 0;
}

static int start_perf_events(struct perf_reader *r)
{
	int i;
	for (i = 0; i < r->nr_cpus; i++) {
		if (ioctl(r->fds[i], PERF_EVENT_IOC_ENABLE) < 0) {
			perror("failed to enable perf");
			return -1;
		}
	}
	return 0;
}

static void try_mount_debugfs(void)
{
	system("/bin/mount -t debugfs none " DEBUGFS_MNT " >/dev/null 2>&1");
}

static void perf_reader_stop(struct perf_reader *r)
{
	int i;

	unmap_perf_buffer(r);
	for (i = 0; i < r->nr_cpus; i++) {
		if (r->fds[i] != -1) {
			close(r->fds[i]);
			r->fds[i] = -1;
		}
	}
}

static int perf_reader_start(struct polled_reader *pr)
{
	struct perf_reader *r = (struct perf_reader*) pr;
	int err;

	try_mount_debugfs();

	err = create_perf_events(r);
	if (err)
		goto err;

	err = map_perf_buffer(r);
	if (err)
		goto err;

	err = start_perf_events(r);
	if (err)
		goto err;

	return 0;
err:
	perf_reader_stop(r);
	return err;
}

struct trace_entry {
	unsigned short		type;
	unsigned char		flags;
	unsigned char		preempt_count;
	int			pid;
	int			lock_depth;
};

#define TASK_COMM_LEN 16
#define LT_BACKTRACEDEPTH 12
/*
 * If you think the alignment is screwed up, you're right.
 * Kernel's ftrace_raw_sched_latencytop is nicely aligned,
 * but perf prefixes it with the u32 size.
 * http://lkml.org/lkml/2010/5/19/53
 * XXX: revisit when/if PERF_SAMPLE_RAW_ALIGNED is introduced.
 */
struct __attribute__ ((__packed__)) sample_event {
	struct perf_event_header header;

	uint32_t raw_size;

	struct trace_entry te;
	/* must match the TP_STRUCT__entry in the kernel */
	char comm[TASK_COMM_LEN];
	pid_t pid;
	pid_t tgid;
	int inter;
	long backtrace[LT_BACKTRACEDEPTH];
	uint64_t delay;
};

static unsigned int mmap_read_head(struct perf_reader *r)
{
	struct perf_event_mmap_page *pc = (struct perf_event_mmap_page*) r->md.base;
	int head;

	head = pc->data_head;
	rmb();

	return head;
}

static void mmap_write_tail(struct perf_reader *r, unsigned int tail)
{
	struct perf_event_mmap_page *pc = (struct perf_event_mmap_page*) r->md.base;
	/*
	 * ensure all reads are done before we write the tail out.
	 */
	/* mb(); */
	pc->data_tail = tail;
}

static int parse_event(const struct sample_event *e)
{
	/* Not interested in ourselves. */
	if (e->pid == mypid)
		return 0;
	/*
	 * Not interested in interruptible sleeps longer than 5 ms,
	 * as they are usually intentional.
	 */
	if (e->inter == 1 && e->delay > 5000000)
		return 0;

	struct back_trace bt;
	bt_init(&bt, e->backtrace, LT_BACKTRACEDEPTH);

	pa_account_latency(app_getPA(),
		e->tgid,  // userspace's pid is kernel's tgid
		e->comm,
		e->delay,
		&bt);

	return 0;
}

static int perf_reader_handle_ready_fd(struct polled_reader *pr)
{
	struct perf_reader *r = (struct perf_reader*) pr;
	unsigned int head = mmap_read_head(r);
	unsigned int old = r->md.prev;
	unsigned char *data = (unsigned char*)r->md.base + pagesize;
	int diff;

	diff = head - old;
	//fprintf(stderr, "head = %d; old = %d; diff = %d\n", head, old, diff);
	if (diff < 0) {
		fprintf(stderr, "WARNING: tail got ahead of head in the mmap buffer\n");
		/*
		 * head points to a known good entry, start there.
		 */
		old = head;
	} else if (diff > (MMAP_PAGES-1) * pagesize) {
		fprintf(stderr, "WARNING: failed to keep up with mmap data\n");
		/*
		 * The buffer is (close to) full, but let's still process it.
		 */
	}

	while (old != head) {
		struct sample_event *event = (struct sample_event*) &data[old & r->md.mask];
		struct sample_event event_copy;

		size_t size = event->header.size;

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((old & r->md.mask) + size != ((old + size) & r->md.mask)) {
			unsigned int offset = old;
			unsigned int len = min(sizeof(*event), size), cpy;
			void *dst = &event_copy;

			do {
				cpy = min(r->md.mask + 1 - (offset & r->md.mask), len);
				memcpy(dst, &data[offset & r->md.mask], cpy);
				offset += cpy;
				dst += cpy;
				len -= cpy;
			} while (len);

			event = &event_copy;
		}

		if (event->header.type == PERF_RECORD_SAMPLE)
			parse_event(event);
		old += size;
	}

	r->md.prev = old;
	mmap_write_tail(r, old);

	return 0;
}

static void perf_reader_fini(struct polled_reader *pr)
{
	struct perf_reader *r = (struct perf_reader*) pr;
	perf_reader_stop(r);
}

static int perf_reader_get_fd(struct polled_reader *pr)
{
	struct perf_reader *r = (struct perf_reader*) pr;
	return r->fds[0];
}

static const struct polled_reader_ops perf_reader_ops = {
	.fini = perf_reader_fini,
	.start = perf_reader_start,
	.get_fd = perf_reader_get_fd,
	.handle_ready_fd = perf_reader_handle_ready_fd,
};

static void perf_reader_init(struct perf_reader *r)
{
	int i;

	r->pr.ops = &perf_reader_ops;

	r->nr_cpus = read_cpu_map();
	for (i = 0; i < r->nr_cpus; i++)
		r->fds[i] = -1;
	r->md.base = MAP_FAILED;

	/* XXX */
	mypid    = getpid();
	pagesize = getpagesize();
}

struct polled_reader *perf_reader_new(void)
{
	struct perf_reader *r;

	r = calloc(1, sizeof(struct perf_reader));
	if (r == NULL)
		return NULL;

	perf_reader_init(r);
	return &r->pr;
}
