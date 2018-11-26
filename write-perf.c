/*
 * Copyright (C) 2018 Loic Poulain <loic.poulain@linaro.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <byteswap.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

static inline double ellapsed_time(struct timespec *start, struct timespec *end)
{
	double t1, t2;

	t1 = start->tv_sec + (long double)start->tv_nsec / (1000 * 1000 * 1000);
	t2 = end->tv_sec + (long double)end->tv_nsec / (1000 * 1000 * 1000);

	return t2 - t1;
}

static void save_tab(char *name, double value[], int count)
{
	unsigned int i;

	FILE *f = fopen(name, "w");
	if (!f)
		return;

	for (i = 0; i < count; i++)
		fprintf(f, "%u\t%lf\n", i, value[i]);

	fclose(f);
}

static void usage(void)
{
	printf("Usage: write_test <file> [options]\n" \
	       "options:\n" \
	       "   -s, --size <arg>\n" \
	       "         buffer size (default: 1000000 byte)\n" \
	       "   -c, --count <arg>\n" \
	       "         buffer count (default: 100)\n"  \
	       "   -F, --fwrite\n" \
	       "         Use fwrite instead of write.\n"
	       "   -U, --nosync\n" \
	       "         Do not flush data from cache to file to complete\n"
	       "   -S, --stats <arg>\n" \
	       "         Save write calls timings to file\n" \
	       "\n" \
	       "write_test /dev/sda -S write_res.txt\n"
	       );

}

static const struct option main_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "size", required_argument, NULL, 's' },
	{ "count", required_argument, NULL, 'c' },
	{ "fwrite", no_argument, NULL, 'F' },
	{ "nosync", no_argument, NULL, 'U' },
	{ "save", required_argument, NULL, 'S' },
	{ },
};


int main(int argc, char *argv[])
{
	int loop, fd = 0, err, size = 1000000, count = 100;
	char *file = NULL, *buf = NULL, *fsave = NULL;
	bool fmode = false, nosync = false;
	size_t total_size;
	FILE *f = NULL;

	struct timespec start_ts, stop_ts, start_w_ts, stop_w_ts, start_s_ts, stop_s_ts;
	double *duration;
	double min_write = 9999;
	double max_write = 0;
	double total;

	for (;;) {
		int opt = getopt_long(argc, argv, "hs:c:FUS:", main_options,
				      NULL);
		if (opt < 0)
			break;

		switch (opt) {
		case 's':
			size = atoi(optarg);
			break;
		case 'c':
			count = atoi(optarg);
			break;
		case 'F':
			fmode = true;
			break;
		case 'U':
			nosync = true;
			break;
		case 'S':
			fsave = optarg;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		default:
			break;
		}
	}

	file = argv[optind++];
	if (!file) {
		fprintf(stderr, "no file path specified\n");
		return -EINVAL;
	}

	printf("writting %d x %d-byte to %s...\n", count, size, file);

	if (!fmode) {
		fd = open(file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			fprintf(stderr, "Unable to open %s\n", file);
			return -EINVAL;
		}
	} else {
		f = fopen(file, "wb");
		if (!f) {
			fprintf(stderr, "Unable to open %s\n", file);
			return -EINVAL;
		}
	}

	buf = malloc(size);
	if (!buf) {
		fprintf(stderr, "Unable to alloc  %d-byte buffer\n", size);
		return -ENOMEM;
	}

	duration = malloc(count * sizeof(*duration));
	if (!duration)
		return -ENOMEM;

	clock_gettime(CLOCK_MONOTONIC_RAW, &start_ts);
	loop = count;
	while (loop--) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_w_ts);

		if (!fmode)
			err = write(fd, buf, size);
		else
			err = fwrite(buf, 1, size, f);

		if (err < 0 || err != size) {
			fprintf(stderr, "write error\n");
			return -EIO;
		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &stop_w_ts);

		/* Stats */
		duration[count - loop - 1] = ellapsed_time(&start_w_ts, &stop_w_ts);
		min_write = MIN(min_write, duration[count - loop - 1]);
		max_write = MAX(max_write, duration[count - loop - 1]);
	}

	if (!nosync) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_s_ts);
		if (!fmode) {
			fsync(fd);
		} else {
			printf("FLUSH\n");
			fflush(f); /* flush userspace buffering */
			fsync(fileno(f)); /* flush kernel cache */
		}
		clock_gettime(CLOCK_MONOTONIC_RAW, &stop_s_ts);
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &stop_ts);
	total = ellapsed_time(&start_ts, &stop_ts);
	total_size = size * count;

	printf("written: %lu bytes\n", total_size);
	printf("duration: %lf seconds\n", total);
	printf("sync-duration: %lf seconds\n", ellapsed_time(&start_s_ts, &stop_s_ts));
	printf("bitrate: %lf MB/s\n", (total_size / 1000 / 1000) / total);

	if (!fmode) {
		close(fd);
	} else {
		fclose(f);
	}

	if (fsave) {
		save_tab(fsave, duration, count);
	}
}
