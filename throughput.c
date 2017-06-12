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
#include <assert.h>
#include <math.h>

static intmax_t timespec_diff_nsec(struct timespec start, struct timespec end)
{
	return (end.tv_sec - start.tv_sec) * (intmax_t)1E9 + (end.tv_nsec - start.tv_nsec);
}

size_t size = 128 << 10;
static int quiet;
static int batch;
static int accuracy = INT_MAX;
static char * tmpname = "throughput.tmp";
static int count = 10;

#define add_number_option(o, desc) do \
{ options[optnum].name = #o; \
	options[optnum].flag = (void *)&o; options[optnum].has_arg = 1; \
	options[optnum].val = -1; description[optnum] = desc; optnum++; } while (0)

#define add_flag_option(n, p, v, desc) do \
{ options[optnum].name = n; \
	options[optnum].flag = (void *)p; options[optnum].has_arg = 0; \
	options[optnum].val = v; description[optnum] = desc; optnum++; } while (0)

static struct option options[100];
static char * description[100];
static int optnum;

int options_init()
{
	memset(options, 0, sizeof(options));
	add_number_option(size, "size of synced block in KB, default is 128 MB");
	add_number_option(count, "number of blocks");
	add_number_option(accuracy, "run till standard deviation is less than specified accuracy in KB/s");
	add_flag_option("quiet", &quiet, 1, "don't print intermediate results");
	add_flag_option("batch", &batch, 1, "print only numbers in KB");
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
			       "\tthroughput <options> [testfile]\n"
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
		tmpname = argv[optind];
	return 0;
}

int main(int argc, char *argv[])
{
	int min = INT_MAX, max = 0, kbps;
	struct timespec start, prev;
	intmax_t T = 0, t;
	void * buf;
	int done = 0, i;

	init(argc, argv);
	buf = malloc(size << 10);
	assert(buf);
	memset(buf, 0, size << 10);

	int tmpfile = open(tmpname, O_TRUNC | O_RDWR | O_CREAT, 0660);
	assert(tmpfile > 0);
	clock_gettime(CLOCK_MONOTONIC, &start);

	for (i = 0; !done; i++) {
		struct timespec now;
		int kbps_cur;

		clock_gettime(CLOCK_MONOTONIC, &prev);
		pwrite(tmpfile, buf, size << 10, 0) + 1;
		fdatasync(tmpfile); // do sync explicitly instead O_DSYNC
		clock_gettime(CLOCK_MONOTONIC, &now);
		t = timespec_diff_nsec(prev, now);
		T += t;
		kbps_cur = round(1E9 * size / t);

		min = MIN(min, kbps_cur);
		max = MAX(max, kbps_cur);

		kbps = round(1E9 * (i + 1) * size / T);
		if (!quiet) {
			if (!batch)
				printf("cur=");
			printf("%d ", kbps_cur);
			if (!batch)
				printf("KB/s, ");
		}
		if (!quiet || i + 1 == count) {
			if (!batch)
				printf("overall=");
			printf("%d", kbps);
			if (!batch)
				printf(" KB/s");
			printf("\n");
		}
		assert(kbps <= max);
		assert(kbps >= min);

		if (count == 1)
			done = 1;
		if (i > 0) {
			// Accordingly Range rule for standard deviation
			// and Standard error of the mean
			int stdev = (max - min) / 4 / sqrt(i);
			if (count && (i + 1 >= count) && stdev <= accuracy)
				done = 1;
			if (i > 0 && (!quiet || done)) {
				if (!batch)
					fprintf(stderr, "stdev=");
				fprintf(stderr, "%d", stdev);
				if (!batch)
					fprintf(stderr, " KB/s");
				fprintf(stderr, "\n");
			}
		}
	}
	close(tmpfile);
	free(buf);
	unlink(tmpname);
	return EXIT_SUCCESS;
}
