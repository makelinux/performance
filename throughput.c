#define _GNU_SOURCE
#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_rstat.h>
#include <pthread.h>
#include <human.h>

static double timespec_diff(struct timespec start, struct timespec end)
{
	return (double)(end.tv_sec - start.tv_sec) + 1E-9 * (end.tv_nsec - start.tv_nsec);
}

size_t size = 128 << 10; // = 128 MB, 1 = KB
static int selftest;
static int quiet;
static int batch;
static int writing;
static int stdev_percent = 10;
static char * tmpname[2] = { ".", NULL};
static int count = 10;
static gsl_rng *r;
static int threads;

#define batch_print(out, f1, f2, args...) do { if (!batch) fprintf(out, f1, ##args); else fprintf(out, f2, ##args); fflush(out);} while (0)

#define add_number_option(o, desc) do \
{ options[optnum].name = #o; \
	options[optnum].flag = (void *)&o; options[optnum].has_arg = 1; \
	options[optnum].val = -1; description[optnum] = desc; optnum++; } while (0)

#define add_flag_option(n, p, v, desc) do \
{ options[optnum].name = n; \
	options[optnum].flag = (void *)p; options[optnum].has_arg = 0; \
	options[optnum].val = v; description[optnum] = desc; optnum++; } while (0)

// error("errno = %d \"%s\"", errno, strerror(errno));
#define check_errno() \
do { \
	if (errno) { \
		error_at_line(0, errno, __FILE__, __LINE__, "%s", __func__); \
		fflush(stderr); \
		errno = 0; \
	} \
} while (0)

static struct option options[100];
static char * description[100];
static size_t optnum;

int options_init()
{
	if (optnum + 1 > sizeof (options) / sizeof ((options)[0]))
		return -1;
	memset(options, 0, sizeof(options));
	add_number_option(size, "size of synced block in KB, default is 128 MB");
	add_number_option(count, "number of blocks");
	add_number_option(stdev_percent, "run till standard deviation is less than specified stdev_percent in \% from the mean value, default=10");
	add_number_option(threads, "run number of threads concurrently");

	add_flag_option("quiet", &quiet, 1, "don't print intermediate results");
	add_flag_option("write", &writing, 1, "write mode, default is read");
	add_flag_option("batch", &batch, 1, "print only numbers in KB");
	add_flag_option("selftest", &selftest, 1, "run internal test on generated data");
	options[optnum].name = strdup("help");
	options[optnum].val = 'h';
	description[optnum] = "provide this help";
	optnum++;
	return optnum;
}

int print_options_description()
{
	size_t i;

	for (i = 0; i < optnum; i ++)
		printf("\t--%s %s \n\t\t%s\n\n", options[i].name, options[i].has_arg ?"<n>": "", description[i]);
	return 0;
}

int expand_arg(char *arg)
{
	if (!arg)
		return 0;
	return strtol(arg, NULL, 0);
}

int init(int argc, char *argv[])
{
	int opt = 0;
	int longindex = 0;

	options_init();
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "h", options, &longindex)) != -1) {
		switch (opt) {
		case 0:
			if (options[longindex].val == -1)
				*options[longindex].flag = expand_arg(optarg);
			break;
		case 'h':
			printf("Throughput measurement utility\n"
			       "Usage:\n"
			       "\tthroughput <options> [target] [second target for comparison]\n"
			       "\n"
			       "target - directory, file or block device.\n"
			       "\n"
			       "options:\n"
			       "\n");
			print_options_description();

			exit(0);
			break;
		default:	/* '?' */
			printf("Error in arguments, -h for help\n");
			exit(EXIT_FAILURE);
		}
	}
	if (optind < argc)
		tmpname[0] = argv[optind];
	if (optind + 1 < argc)
		tmpname[1] = argv[optind + 1];
	gsl_rng_default_seed = time(NULL);
	r = gsl_rng_alloc(gsl_rng_default);

	return 0;
}

struct measure {
	int i;
	char * dest;
	double mean;
	double mean_stdev;
	gsl_rstat_workspace *rstat;
	double T;
	pthread_mutex_t lock;
};

int print_throughput_human_batch(FILE *out, char *name, double tp)
{
	char hbuf[LONGEST_HUMAN_READABLE + 1];
	int fmt = human_autoscale | human_round_to_nearest | human_base_1024 |
		human_space_before_unit | human_SI | human_B;
	int ret;

	if (!batch)
		ret = fprintf(out, "%s = %s/s\n",
			name,
			human_readable(tp * 1024, hbuf, fmt, 1, 1));
		else
			ret = fprintf(out, "%.0f\n", tp);
	fflush(out);
	return ret;
}

int print_result(struct measure * m)
{
	print_throughput_human_batch(stdout, "mean", m->mean);

	if (m->mean_stdev != DBL_MAX)
		batch_print(stdout, "mean_stdev=%.0f %%\n", "%.0f\n", 100 * m->mean_stdev / m->mean);
	return 0;
}

static void * buf;

int run_sample(int tmpfile, double * t)
{
	struct timespec prev, now;
	int ret;

	if (selftest) {
		// simulate TP = 100 MB/s, stdev = 10MB/s
		*t = size / (1E5 + gsl_ran_gaussian(r, 1E4));
		return size << 10;
	}

	clock_gettime(CLOCK_MONOTONIC, &prev);
	if (writing)
		ret = pwrite(tmpfile, buf, size << 10, 0);
	else
		ret = pread(tmpfile, buf, size << 10, 0);
	assert(ret == size << 10);
	check_errno();
	fdatasync(tmpfile); // do sync explicitly instead O_DSYNC
	check_errno();
	clock_gettime(CLOCK_MONOTONIC, &now);
	*t = timespec_diff(prev, now);
	return ret;
}

int measure_init(struct measure * m)
{
	m->mean = 0;
	m->mean_stdev = DBL_MAX;
	m->rstat = gsl_rstat_alloc();
	m->T = 0;
	pthread_mutex_init(&m->lock, NULL);
	return 0;
}

int measure_done(struct measure * m)
{
	if (selftest && !quiet && !batch)
		print_throughput_human_batch(stdout, "stdev", gsl_rstat_sd(m->rstat));
	gsl_rstat_free(m->rstat);
	return 0;
}

int measure_do(struct measure * m)
{
	struct stat sb = {0};
	double t;
	int done = 0, i;
	char fn[100];
	int ret;
	int flags = writing ? O_RDWR : O_RDONLY  | O_DIRECT;

	pthread_mutex_lock(&m->lock);
	m->i++;
	pthread_mutex_unlock(&m->lock);

	stat(m->dest, &sb);
	errno = 0;
	if ((sb.st_mode & S_IFMT) == S_IFDIR)
		flags |= O_TMPFILE | O_RDWR;

	if (!sb.st_mode) {
		flags |= O_CREAT | O_RDWR;
		snprintf(fn, sizeof(fn), "%s-%03d", m->dest, m->i);
	} else
		snprintf(fn, sizeof(fn), "%s", m->dest);

	int tmpfile = open(fn, flags, 0660);
	check_errno();
	assert(tmpfile > 0);

	// write the same data to file, to preserve it
	ret = pread(tmpfile, buf, size << 10, 0);
	// if target file is too short, enlarge it
	if (ret < size << 10)
		pwrite(tmpfile, buf, size << 10, 0);

	for (i = 0; !done; i++) {
		double kbps_cur;

		ret = run_sample(tmpfile, &t);
		assert(ret == (int)(size << 10));

		pthread_mutex_lock(&m->lock);
		m->T += t;
		kbps_cur = size / t;
		gsl_rstat_add(kbps_cur, m->rstat);
		m->mean = (threads ? : 1) * gsl_rstat_n(m->rstat) * size / m->T;
		if (!quiet)
			print_throughput_human_batch(stdout, "cur", kbps_cur);
		if (count == 1)
			done = 1;
		pthread_mutex_unlock(&m->lock);
		if (!i)
			continue;
		m->mean_stdev = gsl_rstat_sd_mean(m->rstat);
		if (count && (i + 1 >= count) && (100 * m->mean_stdev / m->mean) <= stdev_percent)
			done = 1;
		if (!quiet)
			print_result(m);
	}
	close(tmpfile);
	check_errno();
	if (!sb.st_mode)
		unlink(fn);
	check_errno();
	return i;
}

int measure_run(struct measure * m)
{
	struct stat sb = {0};
	int lock = 0;
	int ret;
	int t;
	void *res;

	stat(m->dest, &sb);
	errno = 0;
	if (writing && (sb.st_mode & S_IFMT) == S_IFBLK) {
		lock = open(m->dest, O_EXCL, 0);
		check_errno();
		assert(lock > 0);
	}
	measure_init(m);
	pthread_t * pt;
	pt = calloc(threads, sizeof(pthread_t));
	if (!threads)
		ret = measure_do(m);
	else {
		for (t = 0; t < threads; t++) {
			errno = pthread_create(&pt[t], NULL, (void*)&measure_do, m);
			check_errno();
		}
		for (t = 0; t < threads; t++) {
			errno = pthread_join(pt[t], &res);
			check_errno();
		}
	}
	free(pt);
	measure_done(m);
	if (lock)
		close(lock);
	return ret;
}

int main(int argc, char *argv[])
{
	struct measure m[2] = {{0,},};

	init(argc, argv);
	buf = aligned_alloc(512, size << 10);
	assert(buf);
	memset(buf, 0, size << 10);
	m[0].dest = tmpname[0];
	measure_run(&m[0]);
	if (!tmpname[1]) {
		print_result(&m[0]);
	} else {
		m[1].dest = tmpname[1];
		if (!quiet)
			printf("\n");
		measure_run(&m[1]);
		double change_stdev = 100 * sqrt(pow(m[0].mean_stdev, 2) + pow(m[1].mean_stdev, 2)) / m[0].mean;
		print_throughput_human_batch(stdout, "delta", m[1].mean - m[0].mean);
		print_throughput_human_batch(stdout, "delta_stdev", sqrt(pow(m[0].mean_stdev, 2) + pow(m[1].mean_stdev, 2)));
		batch_print(stdout, "change=%.0f %%\n", "%.0f\n", 100*(m[1].mean - m[0].mean) / m[0].mean);
		batch_print(stderr, "change_stdev=%.0f %%\n", "%.0f\n", change_stdev);
	}

	free(buf);
	return EXIT_SUCCESS;
}
