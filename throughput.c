#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
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

// TODO: gsl_rstat_sd_mean

static double timespec_diff(struct timespec start, struct timespec end)
{
	return (double)(end.tv_sec - start.tv_sec) + 1E-9 * (end.tv_nsec - start.tv_nsec);
}

size_t size = 128 << 10; // = 128 MB, 1 = KB
static int selftest;
static int quiet;
static int batch;
static int stdev_percent = 10;
static char * tmpname[2] = { "throughput.tmp", NULL};
static int count = 10;
static gsl_rng *r;

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
		errno = 0; \
	} \
} while (0)

static struct option options[100];
static char * description[100];
static int optnum;

int options_init()
{
	if (optnum + 1 > sizeof (options) / sizeof ((options)[0]))
		return -1;
	memset(options, 0, sizeof(options));
	add_number_option(size, "size of synced block in KB, default is 128 MB");
	add_number_option(count, "number of blocks");
	add_number_option(stdev_percent, "run till standard deviation is less than specified stdev_percent in %% from mean value");
	add_flag_option("quiet", &quiet, 1, "don't print intermediate results");
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
	int i;

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
			       "\tthroughput <options> [testfile] [second test file for comparison]\n"
			       "\n"
			       "options:\n"
			       "\n");
			print_options_description();
			printf("Samples:\n\nTBD\n\n");

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

int print_result(double mean, double mean_stdev)
{
	if (!batch)
		printf("mean=");
	printf("%.0f", mean);
	if (!batch)
		printf(" KB/s");
	printf("\n");
	if (mean_stdev != DBL_MAX) {
		if (!batch)
			fprintf(stderr, "mean_stdev=");
		fprintf(stderr, "%.0f", (100 * mean_stdev / mean));
		if (!batch)
			fprintf(stderr, "%% ");
		fprintf(stderr, "\n");
	}
}
static void * buf;

int run_sample(int tmpfile, double * t)
{
	struct timespec start, prev;
	struct timespec now;
	int ret;

	if (selftest) {
		// simulate TP = 100 MB/s, stdev = 10MB/s
		*t = size / (1E5 + gsl_ran_gaussian(r, 1E4));
		return size << 10;
	}

	clock_gettime(CLOCK_MONOTONIC, &prev);
	ret = pwrite(tmpfile, buf, size << 10, 0);
	check_errno();
	fdatasync(tmpfile); // do sync explicitly instead O_DSYNC
	check_errno();
	clock_gettime(CLOCK_MONOTONIC, &now);
	*t = timespec_diff(prev, now);
	return ret;
}

int measure(char * dest, double * mean, double * mean_stdev)
{
	double min = DBL_MAX, max = 0;
	double T = 0, t;
	int done = 0, i;
	*mean = 0;
	*mean_stdev = DBL_MAX;

	buf = malloc(size << 10);
	assert(buf);
	memset(buf, 0, size << 10);
	int tmpfile = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0660);
	check_errno();
	assert(tmpfile > 0);
	fallocate(tmpfile, 0, 0, size << 10);
	errno = 0;  // clear and ignore possible error
	gsl_rstat_workspace *rstat = gsl_rstat_alloc();

	for (i = 0; !done; i++) {
		double kbps_cur;
		int ret;

		ret = run_sample(tmpfile, &t);
		assert(ret == (size << 10));

		T += t;
		kbps_cur = size / t;
		gsl_rstat_add(kbps_cur, rstat);

		min = MIN(min, kbps_cur);
		max = MAX(max, kbps_cur);

		*mean = (i + 1) * size / T;
		const double median = gsl_rstat_median(rstat);

/*
		rms = gsl_rstat_rms(rstat);
		num = gsl_rstat_n(rstat);
		double mean = gsl_rstat_mean(rstat);
		double var = gsl_rstat_variance(rstat);
		double sd = gsl_rstat_sd(rstat);
		double skew = gsl_rstat_skew(rstat);
		double kurtosis = gsl_rstat_kurtosis(rstat);
		gsl_rstat_reset(rstat);
*/
		if (!quiet) {
			if (!batch)
				printf("cur=");
			printf("%.0f ", kbps_cur);
			if (!batch)
				printf("KB/s, ");
		}
		assert(*mean <= max);
		assert(*mean >= min);
		if (count == 1)
			done = 1;
		if (!i)
			continue;
		*mean_stdev = gsl_rstat_sd_mean(rstat);
		if (count && (i + 1 >= count) && (100 * *mean_stdev / *mean) <= stdev_percent)
			done = 1;
		if (!quiet)
			print_result(*mean, *mean_stdev);
	}
	gsl_rstat_free(rstat);
	close(tmpfile);
	check_errno();
	free(buf);
	unlink(dest);
	check_errno();
}

int main(int argc, char *argv[])
{
	double mean0, stdev0;
	double mean1, stdev1;

	init(argc, argv);
	measure(tmpname[0], &mean0, &stdev0);
	print_result(mean0, stdev0);
	if (tmpname[1]) {
		measure(tmpname[1], &mean1, &stdev1);
		print_result(mean1, stdev1);

		double change_stdev = 100 * sqrt(pow(stdev0, 2) + pow(stdev1, 2)) / mean1;
		if (!quiet)
			printf("delta=%.0f ", mean1 - mean0);
		if (!batch)
			printf("change_min=");
		printf("%.0f", 100*(mean1 - mean0) / mean0 - 2 * change_stdev);
		if (!batch)
			printf("%%");
		printf("\n");
		if (!batch)
			printf("change_max=");
		printf("%.0f", 100*(mean1 - mean0) / mean0 + 2 * change_stdev);
		if (!batch)
			printf("%%");
		printf("\n");
	}

	return EXIT_SUCCESS;
}
