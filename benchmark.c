/**
 * benchmark.c  - Simple benchmark for cpu caches, to reproduce the cases
 * 		detailed by Igor Ostrovsky in his blog.
 *
 * Author: Sougata Santra (sougata.santra@gmail.com)
 *
 * Reference: (http://igoro.com/archive/gallery-of-processor-cache-effects)
 *
 * More details can be found here:
 *	http://www.intel.com/content/www/us/en/architecture-and-technology/\
 *	64-ia-32-architectures-optimization-manual.html
 *	(chapter 7: Optimizing cache usage)
 *
 */
#define _GNU_SOURCE
#include <sched.h>
#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/resource.h>

static struct rusage susage, eusage;
#define GIGABYTES(x)    ((long long)(x) << 30)
#define MEGABYTES(x)    ((long long)(x) << 20)
#define KILOBYTES(x)    ((long long)(x) << 10)
static void die(const char *str) __attribute__((__noreturn__));

/* Exit program */
static void die(const char *str)
{
	perror(str);
	exit(1);
}

/* Helper for convert bytes to more redable format with prefix. */
static inline void bytes_to_prefix(size_t *bytes, char **s)
{
        size_t b = *bytes;
	size_t mask = (1U << 10) - 1;
        if (*s) {
                if (!(b & mask)) {
                        b >>= 10;
                        *s = "k";
                }
                if (!(b & mask)) {
                        b >>= 10;
                        *s = "M";
                }
                if (!(b & mask)) {
                        b >>= 10;
                        *s = "G";
                }
        }
        *bytes = b;
}

/* Stop timer and return time in usecs. */
static time_t timediff(struct timeval *start)
{
	struct timeval stop;

	if (gettimeofday(&stop, NULL)) 
		die("gettimeofday()");
	return (stop.tv_sec - start->tv_sec) * 1000000 +
		stop.tv_usec - start->tv_usec;
}

/**
 * Invalidate the cache line that contains the linear address specified with
 * @p from all levels of the processor cache hierarchy (data and instruction)
 *
 * Please NOTE: The data is not written-back but invalidated.
 */
static inline void clflush(volatile void *p)
{
	asm volatile ("clflush (%0)" :: "r"(p));
}

/**
 * benchmark_prologue - Set the conditions and timer for the test to begin.
 *
 * Prior to every benchmark we fault the pages of the memory mapped area used
 * for the test, so that we don't have to account for page faults while taking
 * the measurements and also we get the count of hard and soft page faults
 * before we start the timer. Also we get count of voluntary and involumtary
 * context switches which might also have some effect on chaches which are
 * not shared between the chores ( we set a high priority for the benchmark
 * program so there should not be lot of context-switches. If we see lot of
 * context switches the something is not correct.)
 */
static void benchmark_prologue(struct timeval *start, uint8_t *buf, size_t size)
{
	loff_t i;
	const size_t page_size = getpagesize();
	i = 0;
	/*
	 * Attempt to page-in the vm pages before we be begin our test
	 * so that the first run is not affected by page-in and also
	 * TLB is polulated.
	 *
	 * We explicitly make it illegal for the compiler to optimize out the
	 * store instruction by casting the volatile type qualifier so that
	 * there is a memory access causing a page-fault.
	 */
	do {
		((unsigned char volatile *)buf)[i] = buf[i];
		/* Invalidate the cache access, we don't write back the data.*/
		clflush(buf);
		if (size < page_size)
			break;
		size -= page_size;
	} while (i += page_size, size != 0);

	if (getrusage(RUSAGE_SELF, &susage) == -1)
		die("getrusage()");
	if (gettimeofday(start, NULL) == -1)
		die("gettimeofday()");
	
}

/*
 * End timer and get hard and soft page faults during the test run. If results
 * show higher values for page faults, then results will not be very accurate.
 */
static void benchmark_epilogue(struct timeval *start, size_t step)
{
	time_t diff;
	char *prefix;

	diff = timediff(start);
	if (getrusage(RUSAGE_SELF, &eusage) == -1)
		die("getrusage()");
	prefix = " ";
	bytes_to_prefix(&step, &prefix);
	printf("step: %4zu%s, diff: %6lu(us) hf: %2lu, sf %2lu, nvcs: %1lu, "
		"nivcs: %2lu\n", step,
		prefix, diff,
		eusage.ru_majflt - susage.ru_majflt,
		eusage.ru_minflt - susage.ru_minflt,
		eusage.ru_nvcsw - susage.ru_nvcsw,
		eusage.ru_nivcsw - susage.ru_nivcsw
		);
}

/*
 * Explicitly disable any benchmark test function optimization.
 * Using gcc (GCC) 5.3.1 20160406 (Red Hat 5.3.1-6)
 */
#pragma GCC push_options
#pragma GCC optimize ("O0")
static void bench(uint32_t *buf, size_t length, int step)
{
	int i;
	const unsigned char shift = ffs(sizeof(uint32_t)) - 1;
	length >>= shift;

	for ( i = 0; i < length; i+= step)
		buf[i] *= 3;
}

static void bench1(uint32_t *buf, size_t length, int limit)
{
	int i;
	size_t lengthMod = length - 1;

	for (i = 0; i < limit; i++)
		buf[(i * 16) & lengthMod]++;
}

static void bench2(uint32_t *buf, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		buf[0]++;
		buf[0]++;
	}
}

static void bench3(uint32_t *buf, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		buf[0]++;
		buf[1]++;
	}
}
#pragma GCC pop_options

int main(void)
{
	uint32_t *buf;
	size_t size, step;
	struct timeval start;
	cpu_set_t my_set;
	struct sched_param param;
	const int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	/*
 	 * Set CPU affinity so that this process is always scheduled in the same
 	 * cpu core. Scheduling in speparate cores will not account for L1 hits
	 * which is not shared between the chores.
 	 */ 
	CPU_ZERO(&my_set);
	CPU_SET(0, &my_set);
	sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
	/*
	 * Set maximum priority with real time FIFO policy so that the process
	 * does not get preempted too often and gets more CPU usage.
	 */
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (sched_setscheduler(0, SCHED_FIFO, &param))
		die("sched_setscheduler()");

	fprintf(stdout, "\nExample 2: Impact of cache lines. 1\n");

	size = MEGABYTES(64);
	buf = (uint32_t *)mmap(NULL, size, prot, flags, -1, 0);
	if (buf == MAP_FAILED)
		die("mmap()");
	for (step = 1; step <= 4096; step <<= 1) {
		benchmark_prologue(&start, (uint8_t *)buf, size);
		bench(buf, size, step);
		benchmark_epilogue(&start, step);
	}
	if (munmap(buf, size) == -1)
		die("munmap()");

	fprintf(stdout, "\nExample 3: L1 and L2 cache sizes\n");

	for (step = KILOBYTES(1); step <= GIGABYTES(1); step <<= 1) {
		size_t length;

		length = step >> (ffs(sizeof(uint32_t)) - 1);
		size = length * sizeof(uint32_t);
		buf = (uint32_t *)mmap(NULL, size, prot, flags, -1, 0);
		if (buf == MAP_FAILED)
			die("mmap()");
		benchmark_prologue(&start, (uint8_t *)buf, size);
		bench1(buf, length, MEGABYTES(64));
		benchmark_epilogue(&start, step);
		if (munmap(buf, size) == -1)
			die("munmap()");
	}

	fprintf(stdout, "\nExample 4: Instruction-level parallelism\n");

	size = getpagesize();;
	buf = (uint32_t *)mmap(NULL, size, prot, flags, -1, 0);
	if (buf == MAP_FAILED)
		die("mmap()");
	benchmark_prologue(&start, (uint8_t *)buf, size);
	bench2(buf, 256 * 1024 * 1024);
	benchmark_epilogue(&start, 1);
	benchmark_prologue(&start, (uint8_t *)buf, size);
	bench3(buf, 256 * 1024 * 1024);
	benchmark_epilogue(&start, 2);
	if (munmap(buf, size) == -1)
		die("munmap()");
	return 0;
}
